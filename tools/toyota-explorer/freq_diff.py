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
"""
Compare frequency captures, noise floor first.

    freq_diff.py freq_off1.json freq_off2.json freq_tail.json freq_head.json

The first two must be the SAME vehicle state with nothing touched. Whatever
they already disagree about is the noise floor, and any id above it is
disqualified no matter how convincing it looks in the other captures.

Works per bit, not per payload: a state flag is a bit whose duty cycle is
near 0 in one state and near 1 in another, and stable across the control
pair. Counting payloads cannot see that when other bits in the same byte
move independently.
"""
import argparse
import json
import sys


def frame_counts(cap):
    return {cid: sum(p.values()) for cid, p in cap.items()}


def bit_rates(cap):
    """{(canid, byte index, bit): fraction of frames where the bit is 1}"""
    out = {}
    for cid, payloads in cap.items():
        total = sum(payloads.values())
        if total == 0:
            continue
        for hexstr, n in payloads.items():
            try:
                raw = bytes.fromhex(hexstr)
            except ValueError:
                continue
            for bi, byte in enumerate(raw):
                for bit in range(8):
                    if byte >> bit & 1:
                        key = (cid, bi, bit)
                        out[key] = out.get(key, 0.0) + n
        for bi in range(8):
            for bit in range(8):
                key = (cid, bi, bit)
                if key in out:
                    out[key] /= total
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("control_a")
    ap.add_argument("control_b")
    ap.add_argument("states", nargs="+", help="captures of the other states")
    ap.add_argument("--noise", type=float, default=0.05,
                    help="max duty-cycle drift allowed between the controls")
    ap.add_argument("--signal", type=float, default=0.80,
                    help="min duty-cycle change required to call a candidate")
    ap.add_argument("--min-frames", type=int, default=10,
                    help="an id seen fewer times than this in ANY capture "
                         "cannot support a duty-cycle claim at all")
    args = ap.parse_args()

    caps = {p: json.load(open(p))
            for p in [args.control_a, args.control_b] + args.states}
    a = bit_rates(caps[args.control_a])
    b = bit_rates(caps[args.control_b])
    others = [(p, bit_rates(caps[p])) for p in args.states]

    # An id that only shows up a handful of times cannot carry a duty cycle.
    # 0x640 arrives about once every 30s, so 90s of capture gives n=3 and any
    # "0.00 -> 1.00" read off it is one frame pretending to be a measurement.
    counts = {p: frame_counts(c) for p, c in caps.items()}
    thin = {cid for p in caps for cid, n in counts[p].items()
            if n < args.min_frames}
    thin |= {cid for p in caps for cid in
             set().union(*[set(c) for c in caps.values()]) - set(caps[p])}

    keys = set(a) | set(b)
    for _, o in others:
        keys |= set(o)

    keys = {k for k in keys if k[0] not in thin}
    noisy = {k for k in keys if abs(a.get(k, 0.0) - b.get(k, 0.0)) > args.noise}
    print(f"id bi loai vi it mau: {len(thin)}  (<{args.min_frames} frame) -> "
          + ", ".join(sorted(thin)[:14]) + ("..." if len(thin) > 14 else ""))
    print(f"bit theo doi        : {len(keys)}")
    print(f"bit nhieu (loai bo) : {len(noisy)}   "
          f"(lech > {args.noise:.0%} giua {args.control_a} va {args.control_b})")

    hits = []
    for k in sorted(keys - noisy):
        base = (a.get(k, 0.0) + b.get(k, 0.0)) / 2
        for path, o in others:
            if abs(o.get(k, 0.0) - base) >= args.signal:
                hits.append((k, base, [(p, o2.get(k, 0.0)) for p, o2 in others]))
                break

    if not hits:
        print(f"\nKHONG co bit nao vuot nguong {args.signal:.0%}. "
              "Tin hieu khong co tren bus nay, hoac chua bat duoc.")
        return 1

    print(f"\n{len(hits)} ung vien (on dinh o control, doi manh o trang thai khac):")
    hdr = "  %-5s %-4s %-4s %-9s" % ("id", "byte", "bit", "control")
    hdr += "".join(" %-11s" % p.replace("freq_", "").replace(".json", "")
                   for p, _ in others)
    print(hdr)
    for (cid, bi, bit), base, rates in hits:
        row = "  %-5s %-4d %-4d %-9.2f" % (cid, bi, bit, base)
        row += "".join(" %-11.2f" % r for _, r in rates)
        print(row)
    return 0


if __name__ == "__main__":
    sys.exit(main())
