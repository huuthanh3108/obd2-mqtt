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
"""Compare two passive bus captures and report what actually moved."""
import json
import sys


def load(p):
    return json.load(open(p))


def main(a_path, b_path):
    a, b = load(a_path), load(b_path)
    la = a_path.replace("mon_", "").replace(".json", "")
    lb = b_path.replace("mon_", "").replace(".json", "")

    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    both = sorted(set(a) & set(b))

    print(f"A = {la}   B = {lb}\n")

    if only_a or only_b:
        print("CAN ids seen in only one capture (may just be slow periodic frames):")
        if only_a:
            print(f"  only in {la}: {' '.join(only_a)}")
        if only_b:
            print(f"  only in {lb}: {' '.join(only_b)}")
        print()

    print("CAN ids whose payload set differs:")
    strong = []
    for cid in both:
        sa, sb = set(a[cid]), set(b[cid])
        if sa == sb:
            continue
        stable = len(sa) == 1 and len(sb) == 1
        va, vb = sorted(sa)[0], sorted(sb)[0]
        tag = "STABLE" if stable else f"{len(sa)}v/{len(sb)}v"
        print(f"\n  {cid}  [{tag}]")
        if not stable:
            print(f"    {la}: {sorted(sa)[:4]}")
            print(f"    {lb}: {sorted(sb)[:4]}")
            continue
        print(f"    {la}: {va}")
        print(f"    {lb}: {vb}")
        if len(va) != len(vb):
            print("    (different length)")
            continue
        flips = []
        for i in range(0, len(va), 2):
            x, y = int(va[i:i+2], 16), int(vb[i:i+2], 16)
            if x == y:
                continue
            idx = i // 2
            bits = [bit for bit in range(8) if (x ^ y) >> bit & 1]
            print(f"    byte {idx}: {x:02X} -> {y:02X}   {x:08b} -> {y:08b}")
            for bit in bits:
                print(f"      bit {bit}: {(x>>bit)&1} -> {(y>>bit)&1}")
            flips.append((idx, len(bits)))
        total = sum(n for _, n in flips)
        if total <= 2:
            strong.append((cid, flips, va, vb))
            print(f"    *** STRONG: only {total} bit(s) moved in a stable frame")

    print("\n" + "=" * 60)
    if strong:
        print("Best candidates (stable frame, <=2 bits moved):")
        for cid, flips, va, vb in strong:
            for idx, _ in flips:
                print(f"  CAN {cid} byte {idx}:  {va} -> {vb}")
    else:
        print("No stable frame changed by only a bit or two.")
    print("A candidate counts only if it returns to the A value when the")
    print("vehicle returns to state A. Capture the closing state and re-run.")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
