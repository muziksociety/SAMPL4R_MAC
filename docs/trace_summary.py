#!/usr/bin/env python3
"""Summarise a SM5 trace JSON file."""
import json, sys, collections

def summarise(path):
    with open(path) as f:
        data = json.load(f)
    complete = [e for e in data if e.get('ph') == 'X']
    print(f'\n== {path} ==')
    print(f'{len(complete)} complete (scoped) events, {len(data)} total\n')

    # Sort by duration descending
    complete.sort(key=lambda e: e.get('dur', 0), reverse=True)

    print(f'{"CATEGORY":<12} {"NAME":<42} {"DUR (ms)":>12} {"START (ms)":>12} {"TID":>22}')
    print('-' * 110)
    for e in complete[:30]:
        dur_ms = e['dur'] / 1000.0
        ts_ms  = e['ts']  / 1000.0
        cat  = e.get('cat', '?')
        name = e.get('name', '?')
        tid  = e.get('tid', 0)
        print(f'{cat:<12} {name:<42} {dur_ms:>12.2f} {ts_ms:>12.2f} {tid:>22}')

    # Time per category
    print()
    per_cat = collections.defaultdict(float)
    for e in complete:
        per_cat[e.get('cat', '?')] += e['dur'] / 1000.0
    print('Total time per category (ms):')
    for cat, ms in sorted(per_cat.items(), key=lambda kv: -kv[1]):
        print(f'  {cat:<15} {ms:>10.2f}')

    # Thread breakdown
    print()
    per_tid = collections.defaultdict(float)
    for e in complete:
        per_tid[e.get('tid', 0)] += e['dur'] / 1000.0
    print(f'{len(per_tid)} threads seen:')
    for tid, ms in sorted(per_tid.items(), key=lambda kv: -kv[1]):
        print(f'  tid={tid:>22}  busy_ms={ms:>10.2f}')

if __name__ == '__main__':
    for p in sys.argv[1:]:
        summarise(p)
