#!/usr/bin/env python3
"""Re-key WiiCompiled's runtime from Mario Kart Wii (PAL) addresses to Wii Sports Resort (RZTP01) addresses.

Copies external/wiicompiled/runtime (minus third_party/assets) to runtime/wsr/ and rewrites every MKW guest
address it can map:
  - code addresses: through the MKW->WSR function pairing from xmatch (addresses inside a function, e.g.
    jump-table labels, go through the instruction alignment of the pair)
  - data/bss addresses: through votes from aligned data references (lis/addi pairs, r13/r2 small-data
    accesses) inside paired functions
The address spelling is preserved (0x801A65F8, 801A65F8 in macros, _801a65f8 identifier suffixes, func_...).

Anything that can't be mapped is left alone and reported. Native registrations (REGISTER_NATIVE_FUNCTION*,
PPC_NATIVE_OVERRIDE*) whose address can't be mapped are commented out, because registering a hook at an
MKW address would silently hijack an unrelated WSR function.

Output: runtime/wsr/{src,include}, work/reports/rekey.md, projects/wsr/address_map.csv
Re-running overwrites runtime/wsr/{src,include}; pass --out elsewhere once you start hand-editing.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyze  # noqa: E402
import xmatch  # noqa: E402

ROOT = analyze.ROOT
WC = analyze.WIICOMPILED

ADDR_RE = re.compile(r"(?<![0-9A-Fa-fxX])(0x|0X)?(8[01][0-9A-Fa-f]{6})(?![0-9A-Fa-f])")
# Any registration-style macro whose first argument is a guest address (REGISTER_NATIVE_FUNCTION*,
# PPC_NATIVE_OVERRIDE*, GX_FATAL_STUB, ...).
REG_LINE_RE = re.compile(r"^\s*[A-Z][A-Z0-9_]{5,}\(\s*(?:0x)?8[01][0-9A-Fa-f]{6}\b")
MKW_REL_RANGE = (0x805102E0, 0x80900000)  # StaticR.rel (MKW game code, no WSR equivalent)
TEXT_EXT = {".cpp", ".h", ".hpp", ".inc", ".c", ".cc"}



# Macro names the translator's native-registration scan matches (see GeneratedMarkers.cs); renamed inside
# disabled RESORT-UNMAPPED blocks so they stop excluding WSR functions from translation.
UNMAPPED_MACRO_RENAMES = (
    ("PPC_NATIVE_OVERRIDE", "RESORT_UNMAPPED_OVERRIDE"),
    ("REGISTER_NATIVE_FUNCTION", "RESORT_UNMAPPED_REGISTRATION"),
    ("REGISTER_TRANSLATED_FUNCTION", "RESORT_UNMAPPED_TRANSLATED"),
    ("GX_FATAL_STUB", "RESORT_UNMAPPED_GX_STUB"),
)

def section_kind(dol: xmatch.Dol, addr: int, bss: tuple[int, int]) -> str:
    for _, a, s, is_text in dol.sections:
        if a <= addr < a + s:
            return "text" if is_text else "data"
    if bss[0] <= addr < bss[0] + bss[1]:
        return "bss"
    if MKW_REL_RANGE[0] <= addr < MKW_REL_RANGE[1]:
        return "rel"
    return "other"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref-dol", type=Path, default=ROOT / "work" / "ref" / "mkw" / "sys" / "main.dol")
    ap.add_argument("--target-dol", type=Path, default=ROOT / "work" / "extracted" / "sys" / "main.dol")
    ap.add_argument("--target-symbols", type=Path, default=ROOT / "work" / "dtk" / "symbols.txt")
    ap.add_argument("--out", type=Path, default=ROOT / "runtime" / "wsr")
    ap.add_argument("--min-data-votes", type=int, default=1)
    args = ap.parse_args()

    for p in (args.ref_dol, args.target_dol, args.target_symbols, analyze.MKW_MAP):
        if not p.exists():
            sys.exit(f"missing {p} - run setup.sh, extract.sh, extract_ref.sh and analyze.sh first")

    rd, td = xmatch.Dol(args.ref_dol), xmatch.Dol(args.target_dol)
    rdol, tdol = analyze.Dol.load(args.ref_dol), analyze.Dol.load(args.target_dol)
    ref_sda = analyze.find_sda_bases(rdol)[:2]
    tgt_sda = analyze.find_sda_bases(tdol)[:2]

    analyze.log("matching functions (MKW -> WSR)")
    hooks = analyze.collect_hooks()
    pairs, ref, tgt, score = xmatch.run(args.ref_dol, analyze.MKW_MAP, args.target_dol, args.target_symbols,
                                        global_only=analyze.hooked_function_starts(set(hooks)))
    analyze.log(f"{len(pairs)} function pairs")
    analyze.log("deriving data address map from aligned references")
    dmap = xmatch.map_data(rd, td, pairs, ref, tgt, ref_sda, tgt_sda)
    analyze.log(f"{len(dmap)} data addresses mapped")

    ref_starts = sorted(ref)
    tbss = (tdol.bss_addr, tdol.bss_size)
    data_overrides: dict[int, int] = {}
    dov_path = ROOT / "projects" / "wsr" / "data_overrides.txt"
    if dov_path.exists():
        for line in dov_path.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                k, v = line.split()[:2]
                data_overrides[int(k, 16)] = int(v, 16)
    dkeys = sorted(k for k, v in dmap.items() if v[1] >= args.min_data_votes and not v[2])
    mkw_names = analyze.mkw_symbol_names()
    bss = (rdol.bss_addr, rdol.bss_size)

    # Name fallback: WSR names from the analysed map (dtk signatures + MKW cross-match) and projects/wsr/names.txt
    # (hand-identified functions, "hexaddr name" per line, wins over everything else).
    wsr_by_name: dict[str, list[int]] = defaultdict(list)
    manual: dict[str, int] = {}
    wsr_map_path = ROOT / "projects" / "wsr" / "MAP.txt"
    if wsr_map_path.exists():
        for line in wsr_map_path.read_text().splitlines():
            parts = line.split(maxsplit=1)
            if len(parts) == 2 and not parts[1].startswith("0x"):
                wsr_by_name[parts[1].strip()].append(int(parts[0], 16))
    names_path = ROOT / "projects" / "wsr" / "names.txt"
    if names_path.exists():
        for line in names_path.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                addr_s, name = line.split(maxsplit=1)
                manual[name.strip()] = int(addr_s, 16)

    def by_name(a: int) -> tuple[int | None, str]:
        mkw_name = mkw_names.get(a, "")
        if not mkw_name or mkw_name.startswith("0x"):
            return None, ""
        for c in analyze.name_candidates(mkw_name):
            if c in manual:
                return manual[c], "manual"
        for c in analyze.name_candidates(mkw_name):
            hits = wsr_by_name.get(c, [])
            if len(hits) == 1:
                return hits[0], "name"
        return None, ""

    def map_addr(a: int) -> tuple[int | None, str]:
        kind = section_kind(rd, a, bss)
        if kind == "text":
            named, how = by_name(a)
            if named is not None and how == "manual":
                return named, how
            if a in pairs:
                sc = score.get(a, 1.0)
                return pairs[a], "func" if sc >= 1.0 else f"func~{sc:.2f}"
            i = bisect.bisect_right(ref_starts, a) - 1
            if i >= 0:
                parent = ref_starts[i]
                if parent in pairs and a < parent + ref[parent].size:
                    off = xmatch.align_offset(ref[parent], tgt[pairs[parent]], a - parent)
                    if off is not None:
                        return pairs[parent] + off, "inside-func"
            if named is not None:
                return named, how
            return None, "text"
        if kind in ("data", "bss"):
            if a in data_overrides:
                return data_overrides[a], "manual"
            hit = dmap.get(a)
            if hit and hit[1] >= args.min_data_votes:
                return hit[0], f"{kind}({hit[1]}v" + (f",{hit[2]}x" if hit[2] else "") + ")"
            # Struct fields: code usually materialises a struct's base address and reaches fields through
            # offsets, so only the base gets votes. Interpolate between the nearest mapped addresses when
            # both neighbours agree on the displacement (same object layout on both sides).
            i = bisect.bisect_right(dkeys, a)
            lo = dkeys[i - 1] if i > 0 else None
            hi = dkeys[i] if i < len(dkeys) else None
            if lo is not None and hi is not None and a - lo <= 0x100 and hi - a <= 0x100:
                dlo = dmap[lo][0] - lo
                dhi = dmap[hi][0] - hi
                if dlo == dhi and section_kind(td, a + dlo, tbss) in ("data", "bss"):
                    return a + dlo, f"{kind}(interp)"
            # One-sided struct offset: a field past the last mapped address of the same object (e.g. GX fifo
            # object fields at +4..+0x1C, or the saved-state block at +0x80). Accepted only when the result
            # stays below the next mapped anchor on the WSR side, so it cannot land in a different object.
            if lo is not None and a - lo <= 0x800:
                cand = dmap[lo][0] + (a - lo)
                if (hi is None or cand < dmap[hi][0]) and section_kind(td, cand, tbss) in ("data", "bss"):
                    return cand, f"{kind}(offset+{a - lo:#x})"
            return None, kind
        return None, kind  # rel / other: leave alone

    out_root: Path = args.out
    # Full runtime project minus the big vendored trees (third_party/, assets/), which build.sh takes straight
    # from external/wiicompiled. Only src/ and include/ get addresses rewritten.
    for sub in ("src", "include", "cmake", "tests"):
        dst = out_root / sub
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(WC / "runtime" / sub, dst)
    shutil.copyfile(WC / "runtime" / "CMakeLists.txt", out_root / "CMakeLists.txt")

    stats = Counter()
    seen: dict[int, tuple[int | None, str]] = {}
    unresolved: dict[int, list[str]] = defaultdict(list)
    disabled: list[str] = []
    game_id_lines: list[str] = []

    all_files = [f for f in sorted(list((out_root / "src").rglob("*")) + list((out_root / "include").rglob("*")))
                 if f.is_file() and f.suffix in TEXT_EXT]

    # ---- Small-data-relative and computed addresses ---------------------------------------------------------
    # The runtime also reaches guest globals as r13/r2 +/- offset (literal, or through named constants such as
    # kAlarmQueueOffsetFromR13) and as computed literals like "0x801b0000u - 0x43f4". Those encode MKW addresses
    # without spelling them out, so rewrite them here, before the literal-address pass. Computed results are
    # tagged /*wsr*/ so the literal pass leaves them alone.
    sda_rewrites: list[str] = []
    sda_unmapped: list[str] = []
    bases = {"13": (ref_sda[0], tgt_sda[0]), "2": (ref_sda[1], tgt_sda[1])}
    REG_EXPR = re.compile(r"(\br13\b|gpr\[13\]|\br2\b|gpr\[2\])(\s*)([-+])(\s*)(\(?)(-?)(0x[0-9A-Fa-f]+|\d+)(u?)")
    NAMED_USE = re.compile(r"(\br13\b|gpr\[13\]|\br2\b|gpr\[2\])\s*([-+])\s*(k[A-Za-z0-9_]+)")
    COMPUTED = re.compile(r"0x(8[01][0-9a-fA-F]{6})u?\s*([-+])\s*0x([0-9a-fA-F]+)u?")

    def reg_of(tok: str) -> str:
        return "13" if "13" in tok else "2"

    def remap_sda(reg: str, mkw_off_signed: int, where: str) -> int | None:
        rb, tb = bases[reg]
        if rb is None or tb is None:
            return None
        ea = (rb + mkw_off_signed) & 0xFFFFFFFF
        if ea not in seen:
            seen[ea] = map_addr(ea)
        new, how = seen[ea]
        if new is None:
            sda_unmapped.append(f"{where}: r{reg}{mkw_off_signed:+#x} (MKW 0x{ea:08X}, {how})")
            return None
        sda_rewrites.append(f"{where}: r{reg}{mkw_off_signed:+#x} -> r{reg}{new - tb:+#x} (MKW 0x{ea:08X} -> 0x{new:08X}, {how})")
        return new - tb

    named_offsets: dict[str, tuple[str, str]] = {}
    for f in all_files:
        for m in NAMED_USE.finditer(f.read_text(errors="replace")):
            named_offsets[m.group(3)] = (reg_of(m.group(1)), m.group(2))

    for f in all_files:
        rel = f.relative_to(out_root)
        text = f.read_text(errors="replace")
        out_lines = []
        for n, line in enumerate(text.splitlines(keepends=True)):
            where = f"{rel}:{n + 1}"
            if line.lstrip().startswith("//"):
                out_lines.append(line)
                continue

            def sub_expr(m: re.Match) -> str:
                reg, sign, neg, lit = reg_of(m.group(1)), m.group(3), m.group(6), m.group(7)
                val = int(lit, 0)
                if neg:
                    val = -val
                off = val if sign == "+" else -val
                if abs(off) > 0x10000:
                    return m.group(0)
                new_off = remap_sda(reg, off, where)
                if new_off is None:
                    return m.group(0)
                fmt = (lambda v: f"0x{v:x}") if lit.startswith("0x") else str
                op, mag = ("-", -new_off) if new_off < 0 else ("+", new_off)
                return f"{m.group(1)}{m.group(2)}{op}{m.group(4) or ' '}{m.group(5)}{fmt(mag)}{m.group(8)}"

            def sub_computed(m: re.Match) -> str:
                a = int(m.group(1), 16)
                ea = (a + int(m.group(3), 16)) if m.group(2) == "+" else (a - int(m.group(3), 16))
                if ea not in seen:
                    seen[ea] = map_addr(ea & 0xFFFFFFFF)
                new, how = seen[ea]
                if new is None:
                    sda_unmapped.append(f"{where}: computed 0x{ea:08X} ({how})")
                    return m.group(0)
                sda_rewrites.append(f"{where}: computed 0x{ea:08X} -> 0x{new:08X} ({how})")
                return f"0x{new:08X}u /*wsr*/"

            new_line = REG_EXPR.sub(sub_expr, line)
            new_line = COMPUTED.sub(sub_computed, new_line)
            cm = re.match(r"^(\s*(?:static\s+)?(?:inline\s+)?constexpr\s+(?:u?int32_t|int|unsigned)\s+)(k[A-Za-z0-9_]+)(\s*=\s*)(0x[0-9A-Fa-f]+|\d+)(u?)", new_line)
            if cm and cm.group(2) in named_offsets:
                reg, sign = named_offsets[cm.group(2)]
                val = int(cm.group(4), 0)
                off = val if sign == "+" else -val
                new_off = remap_sda(reg, off, where + f" ({cm.group(2)})")
                if new_off is not None:
                    nv = new_off if sign == "+" else -new_off
                    lit = f"0x{nv:X}" if cm.group(4).startswith("0x") else str(nv)
                    new_line = new_line[:cm.start(4)] + lit + new_line[cm.end(4):]
            out_lines.append(new_line)
        new_text = "".join(out_lines)
        if new_text != text:
            f.write_text(new_text)

    # Pre-pass: two MKW hooks can land on the same WSR address (an entry stub and its body, or a wrong fuzzy
    # match). Both registrations would define the same func_XXXXXXXX. Keep the most trustworthy mapping and
    # disable the others.
    def confidence(how: str) -> float:
        if how == "manual":
            return 3.0
        if how == "func":
            return 2.0
        if how == "name":
            return 1.5
        if how.startswith("func~"):
            return float(how[5:])
        return 0.5  # inside-func and anything else
    targets: dict[int, list[tuple[float, str, int]]] = defaultdict(list)
    for f in all_files:
        for n, line in enumerate(f.read_text(errors="replace").splitlines()):
            m = REG_LINE_RE.match(line)
            if not m:
                continue
            a = int(ADDR_RE.search(line[line.index("("):]).group(2), 16)
            new, how = seen.setdefault(a, map_addr(a))
            if new is not None:
                targets[new].append((confidence(how), str(f), n))
    collision_losers: set[tuple[str, int]] = set()
    collisions: list[str] = []
    for new, regs in targets.items():
        if len(regs) > 1:
            regs.sort(key=lambda r: -r[0])
            for _, fn, n in regs[1:]:
                collision_losers.add((fn, n))
            collisions.append(f"0x{new:08X}: kept {Path(regs[0][1]).name}:{regs[0][2] + 1}, disabled "
                              + ", ".join(f"{Path(fn).name}:{n + 1}" for _, fn, n in regs[1:]))

    for f in all_files:
        rel = f.relative_to(out_root)
        lines = f.read_text(errors="replace").splitlines(keepends=True)
        changed = False
        disable_at: list[int] = []
        for n, line in enumerate(lines):
            if re.search(r"0x524[dD]4350", line):
                # 'RMCP' game code (MKW PAL) -> 'RZTP' (Wii Sports Resort PAL)
                line = lines[n] = re.sub(r"0x524[dD]4350", "0x525A5450", line)
                changed = True
                game_id_lines.append(f"{rel}:{n + 1}: (rewritten to RZTP) {line.strip()[:140]}")
            elif "RMCP" in line or "RMCE" in line:
                game_id_lines.append(f"{rel}:{n + 1}: {line.strip()[:140]}")
            if not ADDR_RE.search(line) or "/*wsr*/" in line:
                continue
            bad_hook = False

            def sub(m: re.Match) -> str:
                nonlocal bad_hook
                prefix, hexs = m.group(1) or "", m.group(2)
                a = int(hexs, 16)
                if a < 0x80004000:  # OS globals / exception vectors: identical on every Wii game
                    return m.group(0)
                if a not in seen:
                    seen[a] = map_addr(a)
                new, how = seen[a]
                if new is None:
                    stats[f"unmapped-{how}"] += 1
                    unresolved[a].append(f"{rel}:{n + 1}")
                    if how in ("text", "rel") and REG_LINE_RE.match(line):
                        bad_hook = True
                    return m.group(0)
                stats["mapped"] += 1
                out = f"{new:08X}" if hexs.isupper() or hexs.isdigit() else f"{new:08x}"
                return prefix + out

            new_line = ADDR_RE.sub(sub, line)
            if (str(f), n) in collision_losers:
                bad_hook = True
            if bad_hook:
                disable_at.append(n)
                disabled.append(f"{rel}:{n + 1}: {line.strip()[:140]}")
                new_line = line  # keep the MKW address visible inside the disabled block
            if new_line != line:
                lines[n] = new_line
                changed = True
        if disable_at:
            # Wrap each disabled registration (which may span several lines) in #if 0 ... #endif.
            for start in sorted(disable_at, reverse=True):
                depth, end = 0, start
                for k in range(start, len(lines)):
                    depth += lines[k].count("(") - lines[k].count(")")
                    end = k
                    if depth <= 0:
                        break
                # The translator finds native overrides by scanning source text with regexes and does not
                # evaluate #if, so a disabled MKW registration whose address happens to be a real WSR function
                # (e.g. MKW's StrapScene hook at 0x800077C8) would still exclude that function from
                # translation while no native replacement is compiled in. Rename the macros in the block so
                # the scan cannot see them.
                for k in range(start, end + 1):
                    for old_name, new_name in UNMAPPED_MACRO_RENAMES:
                        lines[k] = lines[k].replace(old_name, new_name)
                lines.insert(end + 1, "#endif // RESORT-UNMAPPED\n")
                lines.insert(start, "#if 0 // RESORT-UNMAPPED: MKW address with no WSR match\n")
            changed = True
        if changed:
            f.write_text("".join(lines))

    # Runtime code that calls translated MKW functions we could not map (e.g. NHTTP, StaticR.rel prologs) would
    # fail to link. Give each missing func_XXXXXXXX a stub that aborts loudly if it is ever reached.
    # Every function in the WSR map gets translated, so the map is the list of func_ symbols that will exist.
    wsr_map = ROOT / "projects" / "wsr" / "MAP.txt"
    have = ({f"FUNC_{line.split()[0].upper()}" for line in wsr_map.read_text().splitlines() if line.strip()}
            if wsr_map.exists() else None)
    natives = set()
    called = set()
    for f in list((out_root / "src").rglob("*.[ch]pp")) + list((out_root / "include").rglob("*")):
        if not f.is_file():
            continue
        text = f.read_text(errors="replace")
        text_active = re.sub(r"#if 0 // RESORT-UNMAPPED.*?#endif // RESORT-UNMAPPED", "", text, flags=re.S)
        natives |= {m.upper() for m in re.findall(r"PPC_NATIVE_OVERRIDE(?:_VOID)?\(\s*([0-9A-Fa-f]{8})", text_active)}
        called |= {m.upper() for m in re.findall(r"\bfunc_([0-9A-Fa-f]{8})\b", text_active)}
    stub_path = out_root / "src" / "resort_unmapped_stubs.cpp"
    missing_calls = sorted(a for a in called if a not in natives and (have is None or f"FUNC_{a}" not in have))
    if have is None:
        warn_msg = "projects/wsr/MAP.txt missing; run analyze.sh first"
        analyze.warn(warn_msg)
        missing_calls = []
    with stub_path.open("w") as fh:
        fh.write("// Generated by scripts/rekey.py: translated MKW functions the runtime calls that have no WSR\n"
                 "// equivalent. Each aborts loudly if reached, so the build links while the port is incomplete.\n"
                 "#include <cstdio>\n#include <cstdlib>\n#include \"ppc_runtime.h\"\n\n")
        for a in missing_calls:
            fh.write(f'extern "C" void func_{a}(CpuContext*) {{\n'
                     f'    std::fprintf(stderr, "[resortcompiled] called unmapped MKW function 0x{a} ({mkw_names.get(int(a, 16), "?")})\\n");\n'
                     f'    std::abort();\n}}\n\n')
    if missing_calls:
        analyze.log(f"{len(missing_calls)} link stub(s) for unmapped MKW functions -> src/resort_unmapped_stubs.cpp")

    # Hand fixes that must survive re-generation live as patches in patches/runtime-wsr/ (paths relative to
    # runtime/wsr, e.g. a/src/system_bridge.cpp), applied in name order.
    patch_dir = ROOT / "patches" / "runtime-wsr"
    for patch in sorted(patch_dir.glob("*.patch")) if patch_dir.exists() else []:
        res = subprocess.run(["patch", "-p1", "--forward", "--no-backup-if-mismatch", "-r", "-", "-d", str(out_root),
                              "-i", str(patch)], capture_output=True, text=True)
        if res.returncode != 0:
            analyze.warn(f"patch {patch.name} did not apply cleanly:\n{res.stdout}{res.stderr}")
        else:
            analyze.log(f"applied {patch.name}")

    # address map CSV
    csv_path = ROOT / "projects" / "wsr" / "address_map.csv"
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["mkw_addr", "wsr_addr", "how", "mkw_symbol", "uses"])
        for a in sorted(seen):
            new, how = seen[a]
            w.writerow([f"0x{a:08X}", f"0x{new:08X}" if new is not None else "", how,
                        mkw_names.get(a, ""), len(unresolved.get(a, [])) or ""])

    # report
    rep = ROOT / "work" / "reports" / "rekey.md"
    rep.parent.mkdir(parents=True, exist_ok=True)
    kinds = Counter(how.split("(")[0].split("~")[0] for new, how in seen.values() if new is not None)
    ukinds = Counter(how for new, how in seen.values() if new is None)
    by_file = Counter(loc.split(":")[0] for locs in unresolved.values() for loc in locs)
    with rep.open("w") as fh:
        fh.write("# Runtime re-key report (MKW RMCP01 -> WSR RZTP01)\n\n")
        fh.write(f"Distinct MKW addresses referenced: **{len(seen)}**, mapped: **{sum(kinds.values())}**\n\n")
        fh.write("Mapped by kind: " + ", ".join(f"{k} {v}" for k, v in kinds.most_common()) + "\n\n")
        fh.write("Unmapped by kind: " + ", ".join(f"{k} {v}" for k, v in ukinds.most_common()) + "\n\n")
        fh.write("- `rel` = MKW StaticR.rel (race/game code) - no WSR equivalent; those features need WSR-specific code.\n"
                 "- `other` = outside MKW's DOL (MEM1 layout constants etc.) - left as is, review by hand.\n\n")
        weak = sorted((float(how.split("~")[1]), a, new) for a, (new, how) in seen.items()
                      if new is not None and "~" in how and float(how.split("~")[1]) < 0.9)
        fh.write(f"## Low-confidence function matches to double-check ({len(weak)})\n\n")
        fh.write("| score | MKW addr | MKW symbol | WSR addr |\n|---|---|---|---|\n")
        for sc, a, new in weak:
            fh.write(f"| {sc:.2f} | 0x{a:08X} | `{mkw_names.get(a, '')}` | 0x{new:08X} |\n")
        fh.write("\n")
        fh.write(f"## r13/r2-relative and computed addresses ({len(sda_rewrites)} rewritten, {len(sda_unmapped)} unmapped)\n\n```\n"
                 + "\n".join(sda_rewrites + ["-- unmapped --"] + sda_unmapped) + "\n```\n\n")
        fh.write(f"## Address collisions ({len(collisions)})\n\n```\n" + "\n".join(collisions) + "\n```\n\n")
        fh.write(f"## Native registrations disabled ({len(disabled)})\n\n")
        fh.write("These hooked MKW code that has no WSR match. Find the WSR equivalent by hand, or drop the hook.\n\n```\n")
        fh.write("\n".join(disabled) + "\n```\n\n")
        fh.write("## MKW game-ID references (numeric RMCP constants are rewritten automatically)\n\n```\n" + "\n".join(game_id_lines) + "\n```\n\n")
        fh.write("## Unresolved references by file\n\n| file | refs |\n|---|---|\n")
        for fn, c in by_file.most_common():
            fh.write(f"| `{fn}` | {c} |\n")
        fh.write("\n## Unresolved addresses\n\n| MKW addr | kind | MKW symbol | first use |\n|---|---|---|---|\n")
        for a in sorted(unresolved):
            fh.write(f"| 0x{a:08X} | {seen[a][1]} | `{mkw_names.get(a, '')}` | {unresolved[a][0]} |\n")

    analyze.log(f"re-keyed runtime written to {out_root.relative_to(ROOT)}/{{src,include}}")
    if collisions:
        analyze.log(f"{len(collisions)} WSR address collision(s) resolved (see report)")
    analyze.log(f"r13/r2-relative + computed addresses: {len(sda_rewrites)} rewritten, {len(sda_unmapped)} unmapped")
    analyze.log(f"addresses: {len(seen)} distinct, {sum(kinds.values())} mapped; "
                f"{len(disabled)} native registrations disabled; report: {rep.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
