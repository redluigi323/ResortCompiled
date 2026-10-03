#!/usr/bin/env python3
"""Resortcompiled analysis pass.

From an extracted Wii Sports Resort disc (work/extracted/), this:
  1. parses main.dol (entry point, sections) and finds the r2/r13 small-data bases from __init_registers
  2. runs decomp-toolkit (dtk dol config + dol split) to discover function boundaries and name
     SDK/library functions from dtk's signature database
  3. writes projects/wsr/MAP.txt in WiiCompiled's "hexaddr name" function-map format
  4. writes projects/wsr/recomp.yml (and a copy inside external/wiicompiled/projects/wsr/)
  5. writes a HLE coverage report: for every MKW-address-keyed hook in WiiCompiled's runtime, resolves the
     MKW symbol name and looks for the same function in WSR -> work/reports/hle_coverage.md + projects/wsr/hle_address_map.csv

Nothing here decompiles anything; dtk is only used as an analyser.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import re
import shutil
import struct
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIICOMPILED = ROOT / "external" / "wiicompiled"
DTK = ROOT / "tools" / "bin" / "dtk"
# WiiCompiled pins this main.dol (clean PAL RMCP01)
MKW_PAL_DOL_SHA256 = "80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05"


def log(msg: str) -> None:
    print(f"\033[1;36m==>\033[0m {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"\033[1;33m[warn]\033[0m {msg}", file=sys.stderr, flush=True)


# --------------------------------------------------------------------------- DOL parsing
@dataclass
class Section:
    kind: str
    file_off: int
    addr: int
    size: int


@dataclass
class Dol:
    data: bytes
    sections: list[Section]
    bss_addr: int
    bss_size: int
    entry: int

    @classmethod
    def load(cls, path: Path) -> "Dol":
        d = path.read_bytes()
        offs = struct.unpack_from(">18I", d, 0x00)
        addrs = struct.unpack_from(">18I", d, 0x48)
        sizes = struct.unpack_from(">18I", d, 0x90)
        secs = []
        for i in range(18):
            if sizes[i]:
                secs.append(Section("text" if i < 7 else "data", offs[i], addrs[i], sizes[i]))
        bss_addr, bss_size, entry = struct.unpack_from(">3I", d, 0xD8)
        return cls(d, secs, bss_addr, bss_size, entry)

    def read_u32(self, addr: int) -> int | None:
        for s in self.sections:
            if s.addr <= addr < s.addr + s.size:
                return struct.unpack_from(">I", self.data, s.file_off + (addr - s.addr))[0]
        return None

    def text_ranges(self) -> list[tuple[int, int]]:
        return [(s.addr, s.addr + s.size) for s in self.sections if s.kind == "text"]


def sext16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def find_sda_bases(dol: Dol) -> tuple[int | None, int | None, int | None]:
    """Follow the first `bl` from the entry point (__init_registers) and decode lis/ori|addi pairs for r2/r13.

    Returns (sda_base r13, sda2_base r2, __init_registers address)."""
    pc = dol.entry
    target = None
    for _ in range(64):
        ins = dol.read_u32(pc)
        if ins is None:
            break
        if (ins >> 26) == 18 and (ins & 3) == 1:  # bl
            li = ins & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            target = (pc + li) & 0xFFFFFFFF
            break
        pc += 4
    if target is None:
        return None, None, None

    hi: dict[int, int] = {}
    val: dict[int, int] = {}
    pc = target
    for _ in range(64):
        ins = dol.read_u32(pc)
        if ins is None or ins == 0x4E800020:  # blr
            break
        op, rd, ra, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
        if op == 15 and ra == 0:  # lis rD, imm
            hi[rd] = (imm << 16) & 0xFFFFFFFF
            val[rd] = hi[rd]
        elif op == 24 and rd == ra and ra in hi:  # ori rA, rS, imm  (rS in rd slot)
            val[ra] = hi[ra] | imm
        elif op == 14 and rd == ra and ra in hi:  # addi rD, rD, simm
            val[rd] = (hi[rd] + sext16(imm)) & 0xFFFFFFFF
        pc += 4
    return val.get(13), val.get(2), target


def has_lowmem_absolute_branch(dol: "Dol", f: "Sym") -> bool:
    size = f.size or 4
    for off in range(0, size, 4):
        w = dol.read_u32(f.addr + off)
        if w is None:
            break
        if (w >> 26) == 18 and (w & 2):  # absolute b/bl
            tgt = w & 0x03FFFFFC
            if tgt < 0x80000000:
                return True
    return False


# --------------------------------------------------------------------------- dtk
SYM_RE = re.compile(
    r"^(?P<name>\S+) = (?:(?P<section>[^:\s]+):)?0x(?P<addr>[0-9A-Fa-f]+); //(?P<attrs>.*)$"
)


@dataclass
class Sym:
    name: str
    section: str | None
    addr: int
    kind: str
    size: int | None


def parse_symbols(path: Path) -> list[Sym]:
    out = []
    for line in path.read_text(errors="replace").splitlines():
        m = SYM_RE.match(line.strip())
        if not m:
            continue
        attrs = dict(
            a.split(":", 1) for a in m.group("attrs").split() if ":" in a
        )
        size = int(attrs["size"], 16) if "size" in attrs else None
        out.append(Sym(m.group("name"), m.group("section"), int(m.group("addr"), 16), attrs.get("type", "label"), size))
    return out


def run(cmd: list[str], **kw) -> None:
    print("   $", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def run_dtk(extracted: Path, dtk_dir: Path, rels: list[Path], jobs: int | None) -> Path:
    import yaml  # python3-yaml

    dtk_dir.mkdir(parents=True, exist_ok=True)
    cfg = dtk_dir / "config.yml"
    main_dol = extracted / "sys" / "main.dol"
    log("dtk dol config")
    run([DTK, "dol", "config", main_dol, *rels, "-o", cfg])

    conf = yaml.safe_load(cfg.read_text())
    conf["symbols"] = str(dtk_dir / "symbols.txt")
    conf["splits"] = str(dtk_dir / "splits.txt")
    conf["write_asm"] = False  # we only want the analysis, not an asm dump
    for mod in conf.get("modules", []) or []:
        stem = Path(mod["object"]).stem
        mod["symbols"] = str(dtk_dir / f"{stem}.symbols.txt")
        mod["splits"] = str(dtk_dir / f"{stem}.splits.txt")
    cfg.write_text(yaml.safe_dump(conf, sort_keys=False))

    # Always analyse from scratch: dtk writes WSR's two read-only data sections both as ".rodata" in
    # splits.txt and then refuses to load that file again ("Multiple sections with name .rodata").
    for stale in dtk_dir.glob("*.txt"):
        stale.unlink()
    shutil.rmtree(dtk_dir / "out", ignore_errors=True)

    log("dtk dol split (analysis; can take a few minutes)")
    cmd = [DTK, "dol", "split", cfg, dtk_dir / "out"]
    if jobs:
        cmd += ["-j", str(jobs)]
    run(cmd)
    sym = dtk_dir / "symbols.txt"
    if not sym.exists():
        sys.exit(f"dtk did not write {sym}")
    return sym



def hidden_entries(dol: Dol, funcs: list[Sym]) -> list[Sym]:
    """Function entries dtk folded into the previous function.

    dtk sizes a function up to the next symbol it knows about. A tiny function that is only ever reached through a
    pointer (typically an empty virtual `blr` in a vtable) and sits right after a function ending in a tail call
    gets swallowed, e.g. WSR 0x802E9C0C (`blr`) inside fn_802E9BBC. The translator then has no entry for it and
    the game dies with "missing indirect jump target" the first time the vtable slot is called.

    Heuristic: a word in a data section that points into the *middle* of a known text function is a hidden entry
    when the instruction before it ends control flow (blr / unconditional b) and the containing function has no
    the word is not one of the function's own jump-table case labels or branch targets.
    """
    import bisect
    text = dol.text_ranges()
    starts = sorted({f.addr for f in funcs})
    size_of = {f.addr: (f.size or 0) for f in funcs}
    def containing(a: int) -> int | None:
        i = bisect.bisect_right(starts, a) - 1
        if i < 0:
            return None
        st = starts[i]
        return st if st < a < st + size_of.get(st, 0) else None
    has_bctr: dict[int, bool] = {}
    def fn_has_bctr(st: int) -> bool:
        if st not in has_bctr:
            has_bctr[st] = any(dol.read_u32(a) == 0x4E800420 for a in range(st, st + size_of[st], 4))
        return has_bctr[st]
    targets: dict[int, set[int]] = {}
    def local_targets(st: int) -> set[int]:
        """Addresses the function's own b/bc instructions jump to - those are labels, not entries."""
        if st not in targets:
            t = set()
            for a in range(st, st + size_of[st], 4):
                ins = dol.read_u32(a) or 0
                op = ins >> 26
                if op == 18 and not ins & 2:
                    t.add((a + ((ins & 0x3FFFFFC) ^ 0x2000000) - 0x2000000) & 0xFFFFFFFF)
                elif op == 16 and not ins & 2:
                    t.add((a + ((ins & 0xFFFC) ^ 0x8000) - 0x8000) & 0xFFFFFFFF)
            targets[st] = t
        return targets[st]
    switches: dict[int, set[int]] = {}
    def switch_targets(st: int) -> set[int]:
        """Case labels of the function's jump tables (lis/addi table base, entries pointing back inside)."""
        if st not in switches:
            t: set[int] = set()
            if fn_has_bctr(st):
                end = st + size_of[st]
                hi: dict[int, int] = {}
                for a in range(st, end, 4):
                    ins = dol.read_u32(a) or 0
                    op, rd, ra, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
                    if op == 15 and ra == 0:                       # lis rd, hi
                        hi[rd] = imm << 16
                    elif op == 14 and ra in hi:                    # addi rd, ra, lo
                        base = (hi[ra] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
                        for k in range(512):
                            e = dol.read_u32(base + 4 * k)
                            if e is None or not st <= e < end:
                                break
                            t.add(e)
            switches[st] = t
        return switches[st]
    found: dict[int, int] = {}
    for sec in dol.sections:
        if sec.kind == "text":
            continue
        n = sec.size // 4
        for w in struct.unpack_from(f">{n}I", dol.data, sec.file_off):
            if w & 3 or not any(a <= w < b for a, b in text):
                continue
            st = containing(w)
            if st is None or w in found:
                continue
            prev = dol.read_u32(w - 4) or 0
            ends_flow = prev == 0x4E800020 or (prev >> 26 == 18 and not prev & 1)  # blr, or b/ba without link
            if ends_flow and w not in local_targets(st) and w not in switch_targets(st):
                found[w] = st
    # A function that swallowed one entry usually swallowed its neighbours too (WSR fn_805703C8 covers seven
    # little methods). Inside those mis-merged ranges, also split where a new stack frame starts right after a
    # blr, and where a tail call (`b` without link) from the function's own head lands past the swallowed entry.
    for st in sorted(set(found.values())):
        end = st + size_of[st]
        for a in range(st + 4, end, 4):
            if a in found:
                continue
            ins = dol.read_u32(a) or 0
            prev = dol.read_u32(a - 4) or 0
            if prev == 0x4E800020 and (ins & 0xFFFF0000) == 0x94210000 and a not in local_targets(st):
                found[a] = st                        # blr; stwu r1,-N(r1)
        first_hidden = min(a for a, s0 in found.items() if s0 == st)
        for a in range(st, first_hidden, 4):
            ins = dol.read_u32(a) or 0
            if ins >> 26 == 18 and not ins & 3:      # plain `b` before the first hidden entry = tail call
                tgt = (a + ((ins & 0x3FFFFFC) ^ 0x2000000) - 0x2000000) & 0xFFFFFFFF
                if first_hidden < tgt < end:
                    found.setdefault(tgt, st)
    return [Sym(f"0x{a:08x}", ".text", a, "function", None) for a in sorted(found)]

# --------------------------------------------------------------------------- outputs
AUTO_NAME = re.compile(r"^(fn|lbl|sub|func)_[0-9A-Fa-f]{8}$")


def write_map(funcs: list[Sym], path: Path) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    seen: set[int] = set()
    lines = []
    for s in sorted(funcs, key=lambda s: s.addr):
        if s.addr in seen:
            continue
        seen.add(s.addr)
        name = f"0x{s.addr:08x}" if AUTO_NAME.match(s.name) else s.name
        lines.append(f"{s.addr:08x} {name}")
    path.write_text("\n".join(lines) + "\n")
    return len(lines)


def write_manifest(path: Path, *, game_id: str, dol: Path, dol_sha: str, entry: int,
                   sda: int | None, sda2: int | None, map_path: Path, out_root: Path, native_root: Path) -> None:
    region = game_id[3] if len(game_id) >= 4 else "P"
    sda_line = f"  sda_base: 0x{sda:08X}" if sda is not None else "  # sda_base: 0x????????   # NOT FOUND - fill in from __init_registers (r13)"
    sda2_line = f"  sda2_base: 0x{sda2:08X}" if sda2 is not None else "  # sda2_base: 0x????????  # NOT FOUND - fill in from __init_registers (r2)"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"""\
# Generated by scripts/analyze.py - WiiCompiled project manifest for Wii Sports Resort.
# Lives in external/wiicompiled/projects/wsr/ so workspace-relative runtime paths resolve.
schema_version: 1
workspace_root: ../..

project:
  id: wsr-{game_id.lower()}
  display_name: Wii Sports Resort
  game_id: {game_id}
  region: {region}

memory:
  base: 0x80000000
  size: 0x01800000   # MEM1 (24 MB). MKW uses 0x01A00000 to cover its REL; revisit if WSR loads modules high.
{sda_line}
{sda2_line}

inputs:
  dol:
    path: {dol}
    sha256: {dol_sha}

translation:
  entry_points:
    - 0x{entry:08X}
  function_map:
    path: {map_path}
  # First pass: emit runtime traps instead of failing on unsupported instructions.
  # Must be false before anything ships.
  allow_unsupported_instructions: true

runtime:
  # WSR-native (HLE) registrations. Must NOT be WiiCompiled's runtime/src (the default): those hooks are keyed
  # to MKW addresses and would silently exclude unrelated WSR code from translation.
  native_registration_root: {native_root}
  native_abi_directories: []

output:
  root: {out_root}
  functions: functions
  runtime_config: RuntimeConfig.h
  data_initializer: data_sections_init.cpp
  base_manifest: base/wsr_base_manifest.json
""")


MKW_MAP = WIICOMPILED / "projects" / "mkwii" / "MAP.txt"


def cross_match(ref_dol: Path, tgt_dol: Path, tgt_symbols: Path, funcs: list[Sym]) -> tuple[list[Sym], dict[int, int]]:
    """Name WSR functions from MKW via xmatch. Returns (renamed funcs, {mkw_addr: wsr_addr})."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import xmatch

    if not MKW_MAP.exists():
        warn("WiiCompiled MKW MAP.txt missing; skipping cross-match")
        return funcs, {}
    sha = hashlib.sha256(ref_dol.read_bytes()).hexdigest()
    if sha != MKW_PAL_DOL_SHA256:
        warn(f"reference DOL sha256 {sha[:16]}... is not clean MKW PAL (RMCP01); names may not line up")
    log("cross-matching against Mario Kart Wii (PAL)")
    hooked = {addr for addr in collect_hooks()} if (WIICOMPILED / "runtime" / "src").exists() else set()
    pairs, ref, tgt, score = xmatch.run(ref_dol, MKW_MAP, tgt_dol, tgt_symbols, global_only=hooked_function_starts(hooked))
    XM_STATE.update(ref=ref, tgt=tgt, score=score)
    names = {t: ref[r].name for r, t in pairs.items() if not xmatch.UNNAMED_RE.match(ref[r].name)}
    out = []
    renamed = 0
    for f in funcs:
        if AUTO_NAME.match(f.name) and f.addr in names:
            f = Sym(names[f.addr], f.section, f.addr, f.kind, f.size)
            renamed += 1
        out.append(f)
    xm = ROOT / "work" / "dtk" / "xmatch.txt"
    xm.parent.mkdir(parents=True, exist_ok=True)
    xm.write_text("".join(f"{t:08x} {r:08x} {score.get(r, 1.0):.3f} {ref[r].name}\n"
                          for r, t in sorted(pairs.items(), key=lambda p: p[1])))
    exact = sum(1 for r in pairs if score.get(r, 1.0) >= 1.0)
    log(f"cross-match: {len(pairs)} functions paired ({exact} exact, {len(pairs) - exact} fuzzy), "
        f"{len(names)} carry MKW names, {renamed} newly named")
    return out, pairs


REG_RE = re.compile(r"REGISTER_NATIVE_FUNCTION(?:_AS)?\(\s*0x([0-9A-Fa-f]{8})\s*,\s*([A-Za-z_]\w*)")
SUFFIX_RE = re.compile(r"\b([A-Za-z_]\w*?)_(8[0-9A-Fa-f]{7})\b")
LABEL_RE = re.compile(r"_(?:switch|caseD_\w+|case_\w+)$")


XM_STATE: dict = {}


def mkw_symbol_names() -> dict[int, str]:
    out: dict[int, str] = {}
    for line in MKW_MAP.read_text().splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            out[int(parts[0], 16)] = parts[1].strip()
    return out


def collect_hooks() -> dict[int, tuple[str, str]]:
    """MKW address -> (runtime identifier, source file) for every address-keyed hook in WiiCompiled's runtime."""
    runtime = WIICOMPILED / "runtime" / "src"
    names = mkw_symbol_names() if MKW_MAP.exists() else {}
    hooks: dict[int, tuple[str, str]] = {}
    for f in sorted(runtime.rglob("*.[ch]pp")) + sorted(runtime.rglob("*.h")):
        text = f.read_text(errors="replace")
        rel = str(f.relative_to(WIICOMPILED))
        for m in REG_RE.finditer(text):
            hooks.setdefault(int(m.group(1), 16), (m.group(2), rel))
        for m in SUFFIX_RE.finditer(text):
            addr = int(m.group(2), 16)
            if addr in names or addr not in hooks:
                hooks.setdefault(addr, (m.group(0), rel))
    return hooks


def hooked_function_starts(hooks: set[int]) -> set[int]:
    """Hook addresses, with jump-table labels replaced by their containing function's start."""
    import bisect
    names = mkw_symbol_names()
    starts = sorted(a for a, n in names.items() if not LABEL_RE.search(n))
    out = set()
    for a in hooks:
        if LABEL_RE.search(names.get(a, "")):
            i = bisect.bisect_right(starts, a) - 1
            if i >= 0:
                out.add(starts[i])
        else:
            out.add(a)
    return out


def name_candidates(n: str) -> list[str]:
    out = [n]
    m = re.sub(r"^RVL::", "", n)
    out.append(m)
    mm = re.match(r"^([A-Z]{2,6})(?:::|__)(\w+)$", m)
    if mm:
        out.append(mm.group(1) + mm.group(2))
    return out


def hle_coverage(wsr_funcs: list[Sym], xpairs: dict[int, int], csv_path: Path, md_path: Path) -> None:
    runtime = WIICOMPILED / "runtime" / "src"
    if not MKW_MAP.exists() or not runtime.exists():
        warn("WiiCompiled checkout missing; skipping HLE coverage report")
        return

    mkw_names = mkw_symbol_names()
    mkw_func_starts = sorted(a for a, n in mkw_names.items() if not LABEL_RE.search(n))
    hooks = collect_hooks()
    xscore = XM_STATE.get("score", {})
    xref, xtgt = XM_STATE.get("ref", {}), XM_STATE.get("tgt", {})

    by_name: dict[str, list[int]] = {}
    for s in wsr_funcs:
        by_name.setdefault(s.name, []).append(s.addr)

    import bisect
    rows = []
    for addr in sorted(hooks):
        ident, src = hooks[addr]
        mkw_name = mkw_names.get(addr) or ""
        is_label = bool(LABEL_RE.search(mkw_name))
        wsr: list[int] = []
        how = ""
        if addr in xpairs:
            sc = xscore.get(addr, 1.0)
            wsr, how = [xpairs[addr]], ("bytes" if sc >= 1.0 else f"fuzzy:{sc:.2f}")
        elif is_label:
            # label inside a function: map through the instruction alignment of the matched parent
            i = bisect.bisect_right(mkw_func_starts, addr) - 1
            parent = mkw_func_starts[i] if i >= 0 else None
            if parent is not None and parent in xpairs:
                pr, pt = xref.get(parent), xtgt.get(xpairs[parent])
                off = addr - parent
                if pr is not None and pt is not None:
                    import xmatch
                    off = xmatch.align_offset(pr, pt, off)
                if off is not None:
                    wsr, how = [xpairs[parent] + off], "label-offset"
        if not wsr and mkw_name and not mkw_name.startswith("0x"):
            for c in name_candidates(mkw_name):
                if c in by_name:
                    wsr, how = by_name[c], "name"
                    break
        status = "matched" if len(wsr) == 1 else ("ambiguous" if wsr else ("label" if is_label else "missing"))
        rows.append({
            "mkw_addr": f"0x{addr:08X}",
            "symbol": mkw_name,
            "wsr_addr": ";".join(f"0x{a:08X}" for a in wsr),
            "status": status,
            "via": how,
            "runtime_identifier": ident,
            "source": src,
        })

    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    counts = Counter(r["status"] for r in rows)
    vias = Counter(("fuzzy" if r["via"].startswith("fuzzy") else r["via"]) for r in rows if r["status"] == "matched")
    per_file = Counter(r["source"] for r in rows if r["status"] != "matched")
    md_path.parent.mkdir(parents=True, exist_ok=True)
    with md_path.open("w") as fh:
        fh.write("# HLE coverage: WiiCompiled hooks vs Wii Sports Resort\n\n")
        fh.write(f"Hooked MKW addresses found in runtime: **{len(rows)}**\n\n")
        for k in ("matched", "ambiguous", "missing", "label"):
            fh.write(f"- {k}: {counts.get(k, 0)}\n")
        fh.write("\nMatched via: " + ", ".join(f"{k} {v}" for k, v in vias.most_common()) + "\n\n")
        fh.write("`fuzzy` = best alignment-based match (score in the CSV `via` column; review anything below ~0.9). ")
        fh.write("`bytes` = masked-instruction match against MKW; `label-offset` = jump-table label inside a byte-matched "
                 "function; `name` = same (normalised) symbol name in WSR.\n"
                 "`missing` = function not found in WSR (different SDK revision, or MKW game code). "
                 "`label` = jump-table label whose parent function wasn't matched.\n\n")
        fh.write("## Unmatched hooks by source file\n\n| file | unmatched |\n|---|---|\n")
        for f, n in per_file.most_common():
            fh.write(f"| `{f}` | {n} |\n")
        fh.write("\n## Missing / ambiguous functions\n\n| MKW addr | symbol | status | WSR candidates |\n|---|---|---|---|\n")
        for r in rows:
            if r["status"] in ("missing", "ambiguous"):
                fh.write(f"| {r['mkw_addr']} | `{r['symbol']}` | {r['status']} | {r['wsr_addr']} |\n")
    log("HLE coverage: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))


# --------------------------------------------------------------------------- main
def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--extracted", type=Path, default=ROOT / "work" / "extracted")
    ap.add_argument("--symbols", type=Path, help="use an existing dtk symbols.txt instead of running dtk")
    ap.add_argument("--jobs", "-j", type=int)
    ap.add_argument("--with-rels", action="store_true",
                    help="also analyse files/**/*.rel (WSR's RELs belong to the MotionPlus movie app, not the game)")
    ap.add_argument("--ref-dol", type=Path, default=ROOT / "work" / "ref" / "mkw" / "sys" / "main.dol",
                    help="Mario Kart Wii PAL main.dol used to carry WiiCompiled's names over (default: work/ref/mkw/sys/main.dol)")
    args = ap.parse_args()

    main_dol = args.extracted / "sys" / "main.dol"
    boot = args.extracted / "sys" / "boot.bin"
    if not main_dol.exists():
        sys.exit(f"{main_dol} not found - run ./scripts/extract.sh first")
    game_id = boot.read_bytes()[:6].decode("ascii", "replace") if boot.exists() else "RZTP01"

    dol = Dol.load(main_dol)
    dol_sha = hashlib.sha256(dol.data).hexdigest()
    log(f"{game_id} main.dol  entry=0x{dol.entry:08X}  sha256={dol_sha[:16]}...")
    for s in dol.sections:
        print(f"   {s.kind:4} 0x{s.addr:08X}-0x{s.addr + s.size:08X} ({s.size:#x})")
    print(f"   bss  0x{dol.bss_addr:08X}-0x{dol.bss_addr + dol.bss_size:08X}")

    sda, sda2, init_regs = find_sda_bases(dol)
    if sda is None or sda2 is None:
        warn("could not decode r13/r2 from __init_registers; fill them in by hand in recomp.yml")
    else:
        log(f"__init_registers @ 0x{init_regs:08X}: _SDA_BASE_ (r13)=0x{sda:08X}  _SDA2_BASE_ (r2)=0x{sda2:08X}")

    all_rels = sorted((args.extracted / "files").rglob("*.rel"))
    rels = all_rels if args.with_rels else []
    if all_rels and not rels:
        log(f"skipping {len(all_rels)} REL(s) under files/ (MotionPlus movie app, not game code); --with-rels to include")

    if args.symbols:
        sym_path = args.symbols
    else:
        if not DTK.exists():
            sys.exit("dtk missing - run ./setup.sh first")
        sym_path = run_dtk(args.extracted, ROOT / "work" / "dtk", rels, args.jobs)

    syms = parse_symbols(sym_path)
    text = dol.text_ranges()
    funcs = [s for s in syms if s.kind == "function" and any(a <= s.addr < b for a, b in text)]
    named = [s for s in funcs if not AUTO_NAME.match(s.name)]
    log(f"dtk: {len(funcs)} functions in main.dol text, {len(named)} named by signature")

    xpairs: dict[int, int] = {}
    if args.ref_dol.exists():
        funcs, xpairs = cross_match(args.ref_dol, main_dol, sym_path, funcs)
        named = [s for s in funcs if not AUTO_NAME.match(s.name)]
        log(f"after MKW cross-match: {len(named)} named functions")
    else:
        warn(f"no MKW reference DOL at {args.ref_dol}; only dtk signature names will be used. "
             "Run ./scripts/extract_ref.sh <MKW PAL image> for far better coverage.")

    # Functions with absolute branches into low memory (e.g. __OSDBJump's `bla 0x60`) are stubs the SDK copies
    # into the exception-vector area at boot; they never run in place and the translator can't follow them.
    lowmem = [f for f in funcs if has_lowmem_absolute_branch(dol, f)]
    if lowmem:
        log("excluding low-memory stubs from the map: " + ", ".join(f"{f.name}@0x{f.addr:08X}" for f in lowmem))
        skip = {f.addr for f in lowmem}
        funcs = [f for f in funcs if f.addr not in skip]

    # Register save/restore helpers. The translator inlines calls into __save_gpr/__restore_gpr/__save_fpr/
    # __restore_fpr when the map names every entry point MKW-style (`_save_gpr_14` .. `_save_gpr_31`, see
    # GuestSaveRestoreThunks.cs). dtk calls them `_savegpr_14` and emits them as labels, so they never reached the
    # map: calls into the middle of the run (e.g. WSR's `bl _savefpr_29` @ 0x800077C8) then had no target.
    thunk_re = re.compile(r"^_(save|rest)(gpr|fpr)_(\d+)$")
    thunks = []
    for s in syms:
        m = thunk_re.match(s.name)
        if m and any(a <= s.addr < b for a, b in text):
            thunks.append(Sym(f"_{m.group(1)}_{m.group(2)}_{m.group(3)}", s.section, s.addr, "function", 4))
    if thunks:
        taken = {t.addr for t in thunks}
        funcs = thunks + [f for f in funcs if f.addr not in taken]
        log(f"named {len(thunks)} register save/restore thunk entries for the translator")

    hidden = hidden_entries(dol, funcs)
    if hidden:
        log(f"adding {len(hidden)} pointer-only function entries dtk folded into their neighbour "
            f"(e.g. {', '.join(f'0x{h.addr:08X}' for h in hidden[:5])})")
        funcs = funcs + hidden

    map_path = ROOT / "projects" / "wsr" / "MAP.txt"
    n = write_map(funcs, map_path)
    log(f"wrote {map_path.relative_to(ROOT)} ({n} entries)")

    manifest_args = dict(
        game_id=game_id, dol=main_dol, dol_sha=dol_sha, entry=dol.entry, sda=sda, sda2=sda2,
        map_path=map_path, out_root=ROOT / "work" / "generated", native_root=ROOT / "runtime" / "wsr" / "src",
    )
    ours = ROOT / "projects" / "wsr" / "recomp.yml"
    write_manifest(ours, **manifest_args)
    if WIICOMPILED.exists():
        theirs = WIICOMPILED / "projects" / "wsr" / "recomp.yml"
        theirs.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ours, theirs)
        log(f"wrote {ours.relative_to(ROOT)} (+ copy in external/wiicompiled/projects/wsr/)")

    hle_coverage(funcs, xpairs, ROOT / "projects" / "wsr" / "hle_address_map.csv",
                 ROOT / "work" / "reports" / "hle_coverage.md")

    # Handy summary of which SDK libraries dtk recognised
    prefixes = Counter()
    for s in named:
        m = re.match(r"^_*([A-Z]{2,5})[A-Z][a-z]", s.name)
        if m:
            prefixes[m.group(1)] += 1
    if prefixes:
        log("recognised SDK prefixes: " + ", ".join(f"{p}:{c}" for p, c in prefixes.most_common(20)))
    print("\nDone. Next: ./scripts/translate.sh")


if __name__ == "__main__":
    main()
