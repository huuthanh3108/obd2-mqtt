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
Passive bus capture via the firmware's ATMA monitor.

Nothing is sent to the vehicle. Every broadcast frame is grouped by CAN id
and the set of distinct payloads per id is recorded, so two captures taken
in two vehicle states can be compared.

This beats the request/response snapshot: a capture takes ~20s instead of
~90s, and it sees everything on the bus rather than the 30 diagnostic PIDs.

    mon_capture.py doors_closed --seconds 20
    mon_capture.py driver_open  --seconds 20
    mon_diff.py mon_doors_closed.json mon_driver_open.json
"""
import argparse
import json
import re
import sys
import time

import serial

PORT, BAUD = "/dev/cu.usbmodem101", 115200

# ELM327 ATMA lines look like "7E8 06 41 00 BE 1F A8 13" with ATH1/ATS0
# in effect they arrive without spaces: "<3 hex id><even number of hex>".
FRAME = re.compile(r"^\[MON\]\s+([0-9A-F]{3})([0-9A-F]*)")


def wait_ready(ser, timeout=120):
    """Wait passively for the firmware to report the ELM327 link is up.

    Strictly no probing. Opening the port resets the board, so the banner is
    guaranteed to come; and any byte we send meanwhile both starves the
    reconnect and leaves a reply in flight that the next command mistakes for
    its own - which is what made ATH1 fail here while it worked from a bare
    serial script.
    """
    print("waiting for the BLE link (passive)...", end="", flush=True)
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n).decode("utf-8", errors="replace")
            if "Connected to ELM327" in buf:
                print(" ready.")
                return True
            buf = buf[-4000:]
        else:
            time.sleep(0.05)
    print(" TIMEOUT")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("--seconds", type=int, default=20)
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = PORT, BAUD, 1
    ser.dtr = ser.rts = False
    ser.open()
    if not wait_ready(ser):
        print("adapter never came up - is the ignition on?")
        return 1

    # The console needs a moment after the link comes up, otherwise the
    # command lands before it is reading and is silently lost.
    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.write(f"mon {args.seconds}\n".encode())
    ser.flush()

    frames, errors, raw_lines = {}, 0, 0
    notes = []
    rawlog = []
    start = time.time()
    end = start + args.seconds + 12
    buf = ""
    while time.time() < end:
        n = ser.in_waiting
        if not n:
            time.sleep(0.02)
            continue
        chunk = ser.read(n).decode("utf-8", errors="replace")
        rawlog.append(chunk)
        buf += chunk
        *lines, buf = buf.split("\n")
        # No retry here on purpose: ANY byte sent to the console stops a
        # running ATMA, so a "helpful" resend silently kills the capture.
        for line in lines:
            line = line.strip()
            if "Captured" in line:
                end = 0
                break
            line = line.replace(" ", "") if line.startswith("[MON]") is False else "[MON] "+line[5:].replace(" ", "")
            m = FRAME.match(line)
            if not m:
                # Never swallow what the firmware says. "[MON] busy ...",
                # "[MON] SEARCHING..." and friends explain an empty capture;
                # hiding them once cost an hour of chasing a phantom.
                if line.startswith("[MON]") or "busy" in line or "BUFFER FULL" in line:
                    notes.append(line)
                continue
            raw_lines += 1
            if "DATA ERROR" in line:
                errors += 1
                continue
            cid, data = m.group(1), m.group(2)
            frames.setdefault(cid, set()).add(data)

    ser.close()

    out = {cid: sorted(vals) for cid, vals in sorted(frames.items())}
    path = f"mon_{args.label}.json"
    with open(path, "w") as f:
        json.dump(out, f, indent=1)

    print(f"saved {path}")
    print(f"{raw_lines} frame line(s), {errors} DATA ERROR, {len(out)} distinct CAN id(s)")
    if notes:
        print("firmware said:")
        for n in dict.fromkeys(notes):
            print(f"  {n}")
    if not out:
        dump = "".join(rawlog)
        print(f"--- raw stream: {len(dump)} chars received ---")
        for l in [x.strip() for x in dump.splitlines() if x.strip()][-25:]:
            print("   ", repr(l))
        print("No frames. Check the lines above before assuming the bus is quiet:")
        print("  'busy'      -> polling task stuck, ECUs probably asleep")
        print("  'SEARCHING' -> ELM327 lost the protocol, ignition off")
    for cid, vals in out.items():
        note = "CONSTANT" if len(vals) == 1 else f"{len(vals)} variants"
        print(f"  {cid}  {note:<14} e.g. {vals[0]}")


if __name__ == "__main__":
    sys.exit(main())
