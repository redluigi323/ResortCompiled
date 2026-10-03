#!/usr/bin/env python3
"""Cross-binary function matching: name functions in a target DOL using a reference DOL + its symbol map.

Used to carry Mario Kart Wii's (WiiCompiled's) function names over to Wii Sports Resort. Both games are built
from closely related RVL SDK / NW4R / EGG libraries, so shared library functions compile to the same
instructions modulo relocated fields (branch targets, lis/addi address halves, r2/r13 small-data offsets).

Algorithm
  1. Fingerprint every function in both binaries: instruction words with relocatable fields masked out.
  2. Seed: a fingerprint that is unique in the reference AND unique in the target is a match.
  3. Propagate: for each matched pair, the n-th `bl` in one corresponds to the n-th `bl` in the other, so
     their callees are paired too (if the callees' fingerprints agree). Repeat until nothing changes.

Standalone use:
  xmatch.py --ref-dol mkw/main.dol --ref-map external/wiicompiled/projects/mkwii/MAP.txt \
            --target-dol work/extracted/sys/main.dol --target-symbols work/dtk/symbols.txt -o work/dtk/xmatch.txt
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

LABEL_RE = re.compile(r"_(?:switch|caseD_\w+|case_\w+)$")
UNNAMED_RE = re.compile(r"^(?:0x[0-9A-Fa-f]{8}|(?:fn|lbl|sub|func|thunk)_(?:0x)?[0-9A-Fa-f]{8})$")

LOADSTORE = set(range(32, 56)) | {46, 47, 56, 57, 60, 61}  # D-form memory ops incl. lmw/stmw, psq_l/st(u)


# --------------------------------------------------------------------------- DOL
class Dol:
    def __init__(self, path: Path):
        d = path.read_bytes()
        self.data = d
        offs = struct.unpack_from(">18I", d, 0x00)
        addrs = struct.unpack_from(">18I", d, 0x48)
        sizes = struct.unpack_from(">18I", d, 0x90)
        self.sections = [(offs[i], addrs[i], sizes[i], i < 7) for i in range(18) if sizes[i]]
        self.entry = struct.unpack_from(">I", d, 0xE0)[0]

    def text_ranges(self) -> list[tuple[int, int]]:
        return [(a, a + s) for _, a, s, t in self.sections if t]

    def words(self, start: int, end: int) -> list[int] | None:
        for off, a, s, _ in self.sections:
            if a <= start and end <= a + s:
                n = (end - start) // 4
                return list(struct.unpack_from(f">{n}I", self.data, off + (start - a)))
        return None

    def in_text(self, addr: int) -> bool:
        return any(a <= addr < b for a, b in self.text_ranges())


# --------------------------------------------------------------------------- fingerprints
def mask_words(words: list[int]) -> tuple[list[int], list[int]]:
    """Return (masked words, indices of bl instructions)."""
    out = []
    bls = []
    hi_regs: set[int] = set()  # registers currently holding a lis high half
    for i, w in enumerate(words):
        op = w >> 26
        rd = (w >> 21) & 31
        ra = (w >> 16) & 31
        m = w
        if op == 18:  # b / bl / ba / bla
            m = w & 0xFC000003
            if w & 1:
                bls.append(i)
        elif op == 15:  # addis / lis
            m = w & 0xFFFF0000
            if ra == 0:
                hi_regs.add(rd)
            else:
                hi_regs.discard(rd)
        elif op in (14, 24, 25):  # addi / ori / oris  (lo halves, sda21 when rA is r2/r13)
            if op == 14 and (ra in (0, 2, 13) or ra in hi_regs):
                m = w & 0xFFFF0000
            elif op in (24, 25) and rd in hi_regs:  # ori rA, rS, imm : rS is the rd slot
                m = w & 0xFFFF0000
            dest = rd if op == 14 else ra
            hi_regs.discard(dest)
        elif op in LOADSTORE:
            if ra in (2, 13) or ra in hi_regs:
                m = w & 0xFFFF0000
            if 32 <= op <= 47 and op % 2 == 0 and op not in (36, 38, 44, 46):  # integer loads write rD
                hi_regs.discard(rd)
        elif op == 31:
            hi_regs.discard(rd)
        out.append(m)
    return out, bls


@dataclass
class Func:
    addr: int
    size: int
    name: str
    fp: str = ""
    bl_targets: list[int] = field(default_factory=list)
    masked: tuple = ()


def fingerprint(dol: Dol, f: Func) -> None:
    words = dol.words(f.addr, f.addr + f.size)
    if not words:
        return
    while len(words) > 1 and words[-1] == 0:  # trailing alignment padding
        words.pop()
    masked, bls = mask_words(words)
    f.masked = tuple(masked)
    f.fp = hashlib.sha1(struct.pack(f">{len(masked)}I", *masked)).hexdigest()
    for i in bls:
        w = words[i]
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        f.bl_targets.append((li if (w & 2) else f.addr + 4 * i + li) & 0xFFFFFFFF)


def data_refs(dol: Dol, f: Func, sda: int | None, sda2: int | None) -> dict[int, int]:
    """{instruction index: effective address} for absolute data references in `f`:
    lis/addi|ori|load-store pairs and r13/r2-relative small-data accesses."""
    words = dol.words(f.addr, f.addr + f.size) or []
    hi: dict[int, int] = {}
    out: dict[int, int] = {}
    for i, w in enumerate(words):
        op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 15 and ra == 0:
            hi[rd] = (imm << 16) & 0xFFFFFFFF
            continue
        base = None
        if op == 14 or op in LOADSTORE:
            if ra in hi:
                base = hi[ra]
            elif ra == 13 and sda is not None:
                base = sda
            elif ra == 2 and sda2 is not None:
                base = sda2
            if base is not None:
                out[i] = (base + simm) & 0xFFFFFFFF
        elif op == 24 and rd in hi:  # ori rA, rS, imm
            out[i] = hi[rd] | imm
        # invalidate registers overwritten by this instruction
        if op in (14, 24, 25) or (32 <= op <= 47 and op % 2 == 0) or op == 31:
            dest = ra if op in (24, 25) else rd
            hi.pop(dest, None)
    return out


def index_map(a: Func, b: Func) -> dict[int, int]:
    if a.fp == b.fp:
        return {i: i for i in range(len(a.masked))}
    m: dict[int, int] = {}
    for i, j, n in difflib.SequenceMatcher(None, a.masked, b.masked, autojunk=False).get_matching_blocks():
        for k in range(n):
            m[i + k] = j + k
    return m


def map_data(rd: Dol, td: Dol, pairs: dict[int, int], ref: dict[int, "Func"], tgt: dict[int, "Func"],
             ref_sda: tuple[int | None, int | None], tgt_sda: tuple[int | None, int | None]
             ) -> dict[int, tuple[int, int, int]]:
    """Derive {ref_data_addr: (tgt_data_addr, votes, conflicting_votes)} from aligned data references."""
    votes: dict[int, dict[int, int]] = defaultdict(lambda: defaultdict(int))
    for r_addr, t_addr in pairs.items():
        r, t = ref[r_addr], tgt[t_addr]
        rr = data_refs(rd, r, *ref_sda)
        if not rr:
            continue
        tr = data_refs(td, t, *tgt_sda)
        im = index_map(r, t)
        for i, ea in rr.items():
            j = im.get(i)
            if j is not None and j in tr:
                votes[ea][tr[j]] += 1
    out = {}
    for ea, cands in votes.items():
        best, n = max(cands.items(), key=lambda kv: kv[1])
        out[ea] = (best, n, sum(cands.values()) - n)
    return out


# --------------------------------------------------------------------------- loaders
def load_map_funcs(dol: Dol, map_path: Path) -> list[Func]:
    """'hexaddr name' map (WiiCompiled format). Sizes derived from the next function start."""
    entries = []
    for line in map_path.read_text(errors="replace").splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            continue
        try:
            addr = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[1].strip()
        if LABEL_RE.search(name) or not dol.in_text(addr):
            continue
        entries.append((addr, name))
    entries.sort()
    funcs = []
    ranges = dol.text_ranges()
    for i, (addr, name) in enumerate(entries):
        sec_end = next(b for a, b in ranges if a <= addr < b)
        nxt = entries[i + 1][0] if i + 1 < len(entries) else sec_end
        end = min(nxt, sec_end)
        if end > addr:
            funcs.append(Func(addr, end - addr, name))
    return funcs


SYM_RE = re.compile(r"^(\S+) = (?:[^:\s]+:)?0x([0-9A-Fa-f]+); //.*type:function.*?(?:size:0x([0-9A-Fa-f]+))?")


def load_dtk_funcs(dol: Dol, symbols_path: Path) -> list[Func]:
    funcs = []
    for line in symbols_path.read_text(errors="replace").splitlines():
        if "type:function" not in line:
            continue
        m = re.match(r"^(\S+) = (?:[^:\s]+:)?0x([0-9A-Fa-f]+);", line)
        s = re.search(r"size:0x([0-9A-Fa-f]+)", line)
        if not m or not s:
            continue
        addr = int(m.group(2), 16)
        if dol.in_text(addr):
            funcs.append(Func(addr, int(s.group(1), 16), m.group(1)))
    return funcs


# --------------------------------------------------------------------------- matching
def similarity(a: Func, b: Func) -> float:
    """Ratio of identical masked instructions after alignment (0..1)."""
    if not a.masked or not b.masked:
        return 0.0
    la, lb = len(a.masked), len(b.masked)
    if min(la, lb) / max(la, lb) < 0.6:
        return 0.0
    if a.fp == b.fp:
        return 1.0
    return difflib.SequenceMatcher(None, a.masked, b.masked, autojunk=False).ratio()


def align_offset(a: Func, b: Func, byte_off: int) -> int | None:
    """Map a byte offset inside ref function `a` to the corresponding offset in `b` via instruction alignment."""
    if a.fp == b.fp:
        return byte_off
    idx = byte_off // 4
    sm = difflib.SequenceMatcher(None, a.masked, b.masked, autojunk=False)
    for i, j, n in sm.get_matching_blocks():
        if i <= idx < i + n:
            return (j + (idx - i)) * 4
    return None


FUZZY_MIN = 0.80      # accept a structurally-implied pair (callee / neighbour) at this similarity
GLOBAL_MIN = 0.90     # accept a global best-candidate pair at this similarity ...
GLOBAL_MARGIN = 0.05  # ... if it beats the runner-up by this much


def match(ref: list[Func], tgt: list[Func], fuzzy: bool = True) -> tuple[dict[int, int], dict[int, float]]:
    """Returns ({ref_addr: tgt_addr}, {ref_addr: similarity})."""
    ref_by_fp: dict[str, list[Func]] = defaultdict(list)
    tgt_by_fp: dict[str, list[Func]] = defaultdict(list)
    for f in ref:
        if f.fp:
            ref_by_fp[f.fp].append(f)
    for f in tgt:
        if f.fp:
            tgt_by_fp[f.fp].append(f)
    ref_at = {f.addr: f for f in ref}
    tgt_at = {f.addr: f for f in tgt}
    ref_sorted = sorted(ref_at)
    tgt_sorted = sorted(tgt_at)
    ref_idx = {a: i for i, a in enumerate(ref_sorted)}
    tgt_idx = {a: i for i, a in enumerate(tgt_sorted)}

    pairs: dict[int, int] = {}
    score: dict[int, float] = {}
    used: set[int] = set()

    def add(r: int, t: int, sc: float) -> None:
        pairs[r] = t
        score[r] = sc
        used.add(t)
        work.append((r, t))

    work: list[tuple[int, int]] = []
    # 1. unique exact fingerprints
    for fp, rs in ref_by_fp.items():
        ts = tgt_by_fp.get(fp)
        if ts and len(rs) == 1 and len(ts) == 1:
            add(rs[0].addr, ts[0].addr, 1.0)

    def propagate(min_sim: float) -> None:
        while work:
            r_addr, t_addr = work.pop()
            r, t = ref_at[r_addr], tgt_at[t_addr]
            cands: list[tuple[int, int]] = []
            # callees: n-th bl <-> n-th bl (only when call counts agree)
            if len(r.bl_targets) == len(t.bl_targets):
                cands += list(zip(r.bl_targets, t.bl_targets))
            # link-order neighbours: libraries are linked in the same object order in both games
            ri, ti = ref_idx[r_addr], tgt_idx[t_addr]
            for d in (-1, 1):
                if 0 <= ri + d < len(ref_sorted) and 0 <= ti + d < len(tgt_sorted):
                    cands.append((ref_sorted[ri + d], tgt_sorted[ti + d]))
            for rc, tc in cands:
                if rc in pairs or tc in used:
                    continue
                rf, tf = ref_at.get(rc), tgt_at.get(tc)
                if not rf or not tf:
                    continue
                sim = similarity(rf, tf) if (min_sim < 1.0) else (1.0 if rf.fp == tf.fp else 0.0)
                if sim >= min_sim:
                    add(rc, tc, sim)

    # 2. exact propagation, then fuzzy propagation
    propagate(1.0)
    if fuzzy:
        work.extend(pairs.items())
        propagate(FUZZY_MIN)

    return pairs, score


def global_fuzzy(ref: list[Func], tgt: list[Func], pairs: dict[int, int], score: dict[int, float],
                 only: set[int] | None = None) -> None:
    """Best-candidate search for still-unmatched reference functions (optionally limited to `only`)."""
    used = set(pairs.values())
    by_len: dict[int, list[Func]] = defaultdict(list)
    for f in tgt:
        if f.addr not in used and f.masked:
            by_len[len(f.masked)].append(f)
    for r in ref:
        if r.addr in pairs or not r.masked or (only is not None and r.addr not in only):
            continue
        n = len(r.masked)
        if n < 6:  # too short to be distinctive
            continue
        lo, hi = int(n * 0.8), int(n * 1.25) + 1
        best: list[tuple[float, Func]] = []
        for L in range(lo, hi):
            for t in by_len.get(L, ()):
                if t.addr in used:
                    continue
                sm = difflib.SequenceMatcher(None, r.masked, t.masked, autojunk=False)
                if sm.real_quick_ratio() < GLOBAL_MIN or sm.quick_ratio() < GLOBAL_MIN:
                    continue
                best.append((sm.ratio(), t))
        best.sort(key=lambda x: -x[0])
        if best and best[0][0] >= GLOBAL_MIN and (len(best) == 1 or best[0][0] - best[1][0] >= GLOBAL_MARGIN):
            pairs[r.addr] = best[0][1].addr
            score[r.addr] = best[0][0]
            used.add(best[0][1].addr)


CALLSITE_MIN_SIM = 0.45  # a call-site vote only needs the callee to be loosely similar


def callsite_votes(ref: list[Func], tgt: list[Func], pairs: dict[int, int], score: dict[int, float],
                   rounds: int = 3) -> int:
    """Pair still-unmatched callees through aligned call sites of already-paired callers.

    For every matched caller pair, instructions are aligned; a `bl` in the reference that lines up with a
    `bl` in the target votes for (ref callee -> target callee). A callee is accepted when its votes agree
    (no competing target with as many votes) and the two bodies are at least loosely similar. This reaches
    functions whose code changed between SDK revisions but that are still called from the same places."""
    ref_at = {f.addr: f for f in ref}
    tgt_at = {f.addr: f for f in tgt}
    added_total = 0
    for _ in range(rounds):
        used = set(pairs.values())
        votes: dict[int, dict[int, int]] = defaultdict(lambda: defaultdict(int))
        for r_addr, t_addr in pairs.items():
            r, t = ref_at[r_addr], tgt_at[t_addr]
            if not r.bl_targets or not t.bl_targets:
                continue
            r_bl = _bl_index(r)
            t_bl = _bl_index(t)
            im = index_map(r, t)
            for i, callee in r_bl.items():
                if callee in pairs:
                    continue
                j = im.get(i)
                if j is not None and j in t_bl and t_bl[j] not in used:
                    votes[callee][t_bl[j]] += 1
        added = 0
        for rc, cands in votes.items():
            ranked = sorted(cands.items(), key=lambda kv: -kv[1])
            best, n = ranked[0]
            if len(ranked) > 1 and ranked[1][1] >= n:
                continue
            rf, tf = ref_at.get(rc), tgt_at.get(best)
            if not rf or not tf or best in used:
                continue
            sim = similarity(rf, tf) if rf.masked and tf.masked else 0.0
            if sim >= CALLSITE_MIN_SIM:
                pairs[rc] = best
                score[rc] = min(sim, 0.999)
                used.add(best)
                added += 1
        added_total += added
        if not added:
            break
    return added_total


def gap_fill(ref: list[Func], tgt: list[Func], pairs: dict[int, int], score: dict[int, float],
             max_gap: int = 8, size_tol: float = 0.5) -> int:
    """Link-order gap filling. Between two consecutive matched reference functions whose partners are also
    consecutive-in-order in the target, if the reference gap and the target gap hold the same number of
    unmatched functions (<= max_gap) with compatible sizes, pair them in order."""
    ref_sorted = sorted(f.addr for f in ref)
    tgt_sorted = sorted(f.addr for f in tgt)
    ref_at = {f.addr: f for f in ref}
    tgt_at = {f.addr: f for f in tgt}
    tgt_idx = {a: i for i, a in enumerate(tgt_sorted)}
    used = set(pairs.values())
    added = 0
    anchors = [(i, a) for i, a in enumerate(ref_sorted) if a in pairs]
    for (i0, a0), (i1, a1) in zip(anchors, anchors[1:]):
        gap_r = ref_sorted[i0 + 1:i1]
        if not gap_r or len(gap_r) > max_gap:
            continue
        j0, j1 = tgt_idx[pairs[a0]], tgt_idx[pairs[a1]]
        if j1 <= j0:
            continue
        gap_t = tgt_sorted[j0 + 1:j1]
        if len(gap_t) != len(gap_r) or any(t in used for t in gap_t) or any(r in pairs for r in gap_r):
            continue
        ok = True
        for r, t in zip(gap_r, gap_t):
            sr, st = ref_at[r].size, tgt_at[t].size
            if min(sr, st) / max(sr, st) < size_tol:
                ok = False
                break
        if not ok:
            continue
        for r, t in zip(gap_r, gap_t):
            pairs[r] = t
            score[r] = min(similarity(ref_at[r], tgt_at[t]), 0.999)
            used.add(t)
            added += 1
    return added


def _bl_index(f: Func) -> dict[int, int]:
    """{instruction index: callee} for the bl instructions of f (same order as bl_targets)."""
    out = {}
    k = 0
    for i, w in enumerate(f.masked):
        if (w >> 26) == 18 and (w & 1):
            if k < len(f.bl_targets):
                out[i] = f.bl_targets[k]
            k += 1
    return out


def run(ref_dol: Path, ref_map: Path, tgt_dol: Path, tgt_symbols: Path, global_only: set[int] | None = None,
        ) -> tuple[dict[int, int], dict[int, Func], dict[int, Func], dict[int, float]]:
    rd, td = Dol(ref_dol), Dol(tgt_dol)
    ref = load_map_funcs(rd, ref_map)
    tgt = load_dtk_funcs(td, tgt_symbols)
    for f in ref:
        fingerprint(rd, f)
    for f in tgt:
        fingerprint(td, f)
    pairs, score = match(ref, tgt)
    if global_only is None or global_only:
        global_fuzzy(ref, tgt, pairs, score, only=global_only)
    for _ in range(3):
        n = callsite_votes(ref, tgt, pairs, score) + gap_fill(ref, tgt, pairs, score)
        if not n:
            break
    return pairs, {f.addr: f for f in ref}, {f.addr: f for f in tgt}, score


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref-dol", type=Path, required=True)
    ap.add_argument("--ref-map", type=Path, required=True)
    ap.add_argument("--target-dol", type=Path, required=True)
    ap.add_argument("--target-symbols", type=Path, required=True)
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    pairs, ref, tgt, score = run(a.ref_dol, a.ref_map, a.target_dol, a.target_symbols)
    named = 0
    with a.out.open("w") as fh:
        for r_addr, t_addr in sorted(pairs.items(), key=lambda p: p[1]):
            name = ref[r_addr].name
            if not UNNAMED_RE.match(name):
                named += 1
            fh.write(f"{t_addr:08x} {r_addr:08x} {score.get(r_addr, 1.0):.3f} {name}\n")
    print(f"matched {len(pairs)} functions ({named} named) of {len(ref)} reference / {len(tgt)} target")


if __name__ == "__main__":
    main()
