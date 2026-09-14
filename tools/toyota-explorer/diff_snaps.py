#!/usr/bin/env python3
#
#  This program is free software; you can use it, redistribute it
#  and / or modify it under the terms of the GNU General Public License
#  (GPL) as published by the Free Software Foundation; either version 3
#  of the License or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program, in a file called gpl.txt or license.txt.
#   If not, write to the Free Software Foundation Inc.,
#   59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
"""Byte and bit level diff between two labelled PID snapshots."""
import json, sys

def data_bytes(frame):
    """Strip the 3-char CAN id and the ISO-TP length byte, keep 61 <pid> <data>."""
    if not frame or len(frame) < 8:
        return None
    return frame[3 + 2:]          # drop "7A9" + "04"

def drift_set(paths):
    """Bits that move between snapshots of the SAME state = noise, not signal."""
    snaps = [json.load(open(p)) for p in paths]
    noisy = set()
    for key in snaps[0]:
        vals = [s.get(key) for s in snaps]
        if any(v is None for v in vals):
            continue
        payloads = [data_bytes(v)[4:] for v in vals]
        if len({len(x) for x in payloads}) != 1:
            continue
        for i in range(0, len(payloads[0]), 2):
            bs = [int(x[i:i+2], 16) for x in payloads]
            x = 0
            for v in bs[1:]:
                x |= bs[0] ^ v
            for bit in range(8):
                if x & (1 << bit):
                    noisy.add((key, i // 2 + 1, bit))
    return noisy


def main(a_path, b_path, noise_paths=()):
    noisy = drift_set([a_path, *noise_paths]) if noise_paths else set()
    if noisy:
        print(f"noise mask: {len(noisy)} bit(s) known to drift on their own, suppressed\n")
    a = json.load(open(a_path)); b = json.load(open(b_path))
    la = a_path.replace("snap_", "").replace(".json", "")
    lb = b_path.replace("snap_", "").replace(".json", "")
    changed = 0
    unread = []
    for key in sorted(set(a) | set(b)):
        fa, fb = a.get(key), b.get(key)
        if fa == fb:
            continue
        if fa is None or fb is None:
            # A failed read is not a state change. Never let it become one.
            unread.append(key)
            continue
        da, db = data_bytes(fa), data_bytes(fb)
        if da is None or db is None or len(da) != len(db):
            print(f"\n{key}: {fa} -> {fb}  (length or read differs)")
            changed += 1
            continue
        # skip the "61 <pid>" echo, diff only payload
        pa, pb = da[4:], db[4:]
        if pa == pb:
            continue
        changed += 1
        print(f"\n{key}")
        print(f"  {la:<14} {pa}")
        print(f"  {lb:<14} {pb}")
        for i in range(0, min(len(pa), len(pb)), 2):
            ba, bb = int(pa[i:i+2], 16), int(pb[i:i+2], 16)
            if ba == bb:
                continue
            idx = i // 2 + 1
            print(f"  BYTE {idx}: {ba:02X} -> {bb:02X}   {ba:08b} -> {bb:08b}")
            x = ba ^ bb
            for bit in range(7, -1, -1):
                if x & (1 << bit):
                    if (key, idx, bit) in noisy:
                        print(f"    bit {bit}: {(ba>>bit)&1} -> {(bb>>bit)&1}   [noise, ignored]")
                    else:
                        print(f"    bit {bit}: {(ba>>bit)&1} -> {(bb>>bit)&1}"
                              f"   *** CANDIDATE: {key} byte={idx} bit={bit}")
    print(f"\n{'='*50}\n{changed} PID(s) differ between '{la}' and '{lb}'")
    if changed == 0:
        print("Identical - nothing moved.")
    if unread:
        print(f"\n{len(unread)} PID(s) unread in one snapshot, IGNORED (not a change):")
        print("  " + ", ".join(unread))
        print("  re-capture if you need them.")

if __name__ == "__main__":
    # usage: diff_snaps.py A B [same-state-as-A snapshots ...]
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
