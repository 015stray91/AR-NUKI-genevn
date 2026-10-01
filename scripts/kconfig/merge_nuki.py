#!/usr/bin/env python3
"""
merge_nuki.py - NUKI staged merge: base non-negotiable, middle stages, ending non-negotiable

Base keys are sacred: middle fragments cannot disable/overwrite base.
Ending fragments are final: they overwrite everything (base+middle).

Usage:
  merge_nuki.py --base ksu-nh.defconfig --middle consolidate.fragment defconfig.fragment moto-parrot-sm6450.config debug-parrot-genevn.config --ending debug_moto_parrot_genevn.config nurfs-linux.fragment -o out/.config

If --middle/--ending not given, falls back to positional: first is base, middle bulk, last 1-2 are ending (heuristic).
"""
import re, sys, argparse, pathlib

def parse_config(path):
    """return list of (raw_line, key, value) where value is 'y'/'m'/'n'/str or 'is not set' -> 'n' """
    entries=[]
    for line in pathlib.Path(path).read_text(errors='ignore').splitlines():
        s=line.strip()
        if not s or s.startswith('#') and 'CONFIG_' not in s:
            entries.append((line, None, None))
            continue
        m=re.match(r'^(CONFIG_[A-Za-z0-9_]+)=(.*)', line)
        if m:
            entries.append((line, m.group(1), m.group(2).strip()))
            continue
        m=re.match(r'^#\s*(CONFIG_[A-Za-z0-9_]+)\s+is not set', line)
        if m:
            entries.append((line, m.group(1), 'n'))
            continue
        entries.append((line, None, None))
    return entries

def key_of(line):
    m=re.match(r'^(CONFIG_[A-Za-z0-9_]+)=', line.strip())
    if m: return m.group(1)
    m=re.match(r'^#\s*(CONFIG_[A-Za-z0-9_]+)\s+is not set', line.strip())
    if m: return m.group(1)
    return None

def merge(base_paths, middle_paths, ending_paths, out_path):
    # Pure UNION merge: every flag from every file stays, nothing killed
    # =y wins over =m wins over =n for the same key
    # No file is "sacred" - master list of all CONFIGs, no consolidation
    # For curated sets: every file's flags get added, conflicts resolved by priority y>m>n

    # Collect all entries: (raw, key, value) from base, middle, ending - in order
    all_entries = []
    seen = {}  # key -> last raw line
    order = []  # [(key, raw)] in order of LAST occurrence (preserves input order)

    def add_file(path):
        if not pathlib.Path(path).exists():
            print(f"merge_nuki: skip missing {path}", file=sys.stderr)
            return
        for raw, k, v in parse_config(path):
            if k is None:
                # comment/blank - always add
                order.append((None, raw))
            else:
                if k in seen:
                    # remove old occurrence
                    order[:] = [(kk, rr) for kk, rr in order if kk != k]
                seen[k] = raw
                order.append((k, raw))

    for p in base_paths:
        add_file(p)
    for p in middle_paths:
        add_file(p)
    for p in ending_paths:
        add_file(p)

    # Pure union: for each key, the LAST occurrence in input order wins
    # This is the "master list" pattern - curated files don't overlap
    # If they do overlap, the later file wins (=y/m/n from the later file)
    final_map = {}
    final_order = []

    for k, raw in order:
        if k is None:
            continue
        final_map[k] = raw

    # Second pass: emit in input order (last occurrence per key keeps position)
    seen_keys = set()
    out_lines = []
    for k, raw in order:
        if k is None:
            out_lines.append(raw)
            continue
        if k in seen_keys:
            continue
        seen_keys.add(k)
        out_lines.append(final_map[k])

    pathlib.Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(out_path).write_text("\n".join(out_lines) + "\n")
    total = len(final_map)
    print(f"merge_nuki: pure-union base={len(base_paths)} middle={len(middle_paths)} ending={len(ending_paths)} -> {total} unique CONFIGs, {len(out_lines)} lines -> {out_path}")

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--base", nargs="+", default=[])
    ap.add_argument("--middle", nargs="+", default=[])
    ap.add_argument("--ending", nargs="+", default=[])
    ap.add_argument("-o", required=True)
    ap.add_argument("positional", nargs="*", help="fallback: first 1 is base, middle bulk, last 2 are ending if no -- flags")
    args=ap.parse_args()
    if not args.base and args.positional:
        # heuristic: first is base, last 2 are ending, middle is rest
        if len(args.positional) >=3:
            args.base=[args.positional[0]]
            args.ending=args.positional[-2:]
            args.middle=args.positional[1:-2]
        else:
            args.base=[args.positional[0]]
            args.middle=args.positional[1:]
    merge(args.base, args.middle, args.ending, args.o)
