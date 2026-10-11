#!/usr/bin/env python3
"""Validate function-atlas packages against the package address lists.

Checks, per package: every address has exactly one JSON object and one markdown entry,
statuses are from the allowed set, excluded/library entries carry a reason, and no
entry admits undetermined behavior. Optionally prints aggregate coverage.

Usage: verify_atlas.py --packages <packages.json> [--atlas-dir docs/research/atlas]
       [--evidence-dir docs/research/evidence/atlas] [--only P03,P36]
The packages file lives in the private workspace (it lists addresses of the private
analysis); the atlas outputs live in the project.
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

STATUSES = {"specified", "covered", "excluded", "library"}
FORBIDDEN = re.compile(r"\b(TODO|TBD|unknown behaviou?r|not determined|unclear what)\b", re.I)


def norm(a):
    return hex(int(a, 16))


def check(pkg, atlas_dir, ev_dir):
    errors = []
    pid = pkg["id"].lower()
    md_path = atlas_dir / f"{pid}.md"
    js_path = ev_dir / f"{pid}.json"
    if not md_path.exists() or not js_path.exists():
        return None, [f"{pkg['id']}: missing output"]
    want = {norm(a) for a in pkg["addrs"]}
    data = json.loads(js_path.read_text())
    got = Counter(norm(o["addr"]) for o in data)
    for a in sorted(want - set(got), key=lambda x: int(x, 16)):
        errors.append(f"{pkg['id']}: {a} missing in JSON")
    for a, n in got.items():
        # Storm exports several ordinals at one address; distinct ordinals are not duplicates.
        if n > 1 and len({o.get("ordinal") for o in data if norm(o["addr"]) == a}) < n:
            errors.append(f"{pkg['id']}: {a} duplicated in JSON")
        if a not in want:
            errors.append(f"{pkg['id']}: {a} not in package")
    statuses = Counter()
    reviewed = sum(1 for o in data if (o.get("review") or {}).get("status") in ("confirmed", "corrected"))
    statuses["_reviewed"] = reviewed
    for o in data:
        st = o.get("status")
        statuses[st] += 1
        if st not in STATUSES:
            errors.append(f"{pkg['id']}: {o['addr']} bad status {st!r}")
        if st in ("excluded", "library") and not (o.get("notes") or "").strip():
            errors.append(f"{pkg['id']}: {o['addr']} {st} without reason in notes")
    md = md_path.read_text()
    md_addrs = {norm(m) for m in re.findall(r"^#{2,4} .*?\b(0x[0-9a-fA-F]{5,8})\b", md, re.M)}
    md_addrs |= {norm(m) for m in re.findall(r"`?(0x[0-9a-fA-F]{6,8})`?", md)}
    for a in sorted(want - md_addrs, key=lambda x: int(x, 16)):
        errors.append(f"{pkg['id']}: {a} missing in markdown")
    for m in FORBIDDEN.finditer(md):
        line = md[: m.start()].count("\n") + 1
        errors.append(f"{pkg['id']}: undetermined wording at {md_path.name}:{line}: {m.group(0)}")
    return statuses, errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--packages", type=Path, default=Path("docs/research/evidence/atlas/packages.json"))
    ap.add_argument("--atlas-dir", type=Path, default=Path("docs/research/atlas"))
    ap.add_argument("--evidence-dir", type=Path, default=Path("docs/research/evidence/atlas"))
    ap.add_argument("--only")
    args = ap.parse_args()
    pkgs = json.loads(args.packages.read_text())
    if args.only:
        sel = set(args.only.split(","))
        pkgs = [p for p in pkgs if p["id"] in sel]
    total = Counter()
    all_errors = []
    done = 0
    for p in pkgs:
        st, errs = check(p, args.atlas_dir, args.evidence_dir)
        all_errors += errs
        if st is not None:
            done += 1
            total.update(st)
            entries = sum(v for k, v in st.items() if k != "_reviewed")
            print(f"{p['id']}: {entries} entries {dict(st)} errors={len(errs)}")
        else:
            print(f"{p['id']}: not written")
    for doc in sorted(args.atlas_dir.glob("*.md")):
        text = doc.read_text()
        for m in re.finditer(r"\]\(([^)#\s]+)(#[^)]*)?\)", text):
            target = m.group(1)
            if target.startswith(("http://", "https://")):
                continue
            if not (doc.parent / target).resolve().exists():
                all_errors.append(f"{doc.name}: missing link target {target}")
        if re.search("[\u0400-\u04ff]", text):
            all_errors.append(f"{doc.name}: non-English (Cyrillic) text")
    print(f"packages written {done}/{len(pkgs)}; totals {dict(total)}; errors {len(all_errors)}")
    for e in all_errors[:200]:
        print("  " + e)
    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main())
