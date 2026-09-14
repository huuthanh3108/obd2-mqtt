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
Poll a few enhanced PIDs continuously and print only what changes.

A 90 second snapshot is useless for catching a door toggle - the vehicle
drifts too much in between. This reads the same PIDs over and over so a
real switch shows up as a flip that lines up with the moment you moved
something.

Usage:
    watch.py 7C0.2123 7C0.2122 7C0.2129        # watch these PIDs
    watch.py --all-7c0                          # every PID of ECU 7C0
    watch.py 7C0.2123 --seconds 120
"""
import argparse
import re
import sys
import time

import serial

PORT, BAUD = "/dev/cu.usbmodem101", 115200
RX = re.compile(r"RX\s+([0-9A-F]+)\s")

ECU_PIDS = {
    "7A1": ["03", "06", "07", "08", "0A", "0B", "0C", "15",
            "21", "22", "23", "24", "25", "26", "27", "28", "29"],
    "7B0": ["03", "08", "1F", "21", "3D", "3E", "3F"],
    "7C0": ["12", "13", "21", "22", "23", "29"],
}


def send(ser, cmd, wait, until=None):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    ser.flush()
    end, buf = time.time() + wait, b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            if until and until in buf.decode("utf-8", errors="replace"):
                break
        else:
            time.sleep(0.02)
    return buf.decode("utf-8", errors="replace")


def payload(frame):
    """Data bytes only: drop CAN id, ISO-TP length, the 61 echo and the PID."""
    return frame[7:] if frame and len(frame) > 7 else None


def wait_ready(ser, timeout=120):
    """Block until the adapter is actually reachable.

    Mostly passive on purpose. The firmware's reconnect path is skipped while
    the console holds the OBD pause, and every command takes that pause - so
    probing in a tight loop starves the very reconnect we are waiting for.
    Measured: passive wait reconnects in ~15s, 2s-interval probing never did.
    """
    print("waiting for the BLE link...", end="", flush=True)
    end = time.time() + timeout
    buf = ""
    last_probe = 0.0
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n).decode("utf-8", errors="replace")
            if "Connected to ELM327" in buf:
                print(" ready.")
                time.sleep(1.0)
                ser.reset_input_buffer()
                return True
            if "status=" in buf:
                print(" ready.")
                ser.reset_input_buffer()
                return True
            buf = buf[-4000:]
        else:
            time.sleep(0.05)
        # The board may have been up all along, in which case that banner
        # never comes. Probe rarely enough not to block a reconnect.
        if time.time() - last_probe > 20:
            last_probe = time.time()
            print(".", end="", flush=True)
            ser.write(b"raw 010C" + bytes([10]))
            ser.flush()
    print(" TIMEOUT")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pids", nargs="*", help="e.g. 7C0.2123")
    ap.add_argument("--seconds", type=int, default=180)
    for ecu in ECU_PIDS:
        ap.add_argument(f"--all-{ecu.lower()}", action="store_true")
    args = ap.parse_args()

    targets = list(args.pids)
    for ecu, pids in ECU_PIDS.items():
        if getattr(args, f"all_{ecu.lower()}"):
            targets += [f"{ecu}.21{p}" for p in pids]
    if not targets:
        ap.error("give at least one PID or --all-7c0")

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = PORT, BAUD, 1
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(1.0)
    ser.reset_input_buffer()
    if not wait_ready(ser):
        print('adapter never came up - is the ignition on?')
        return 1
    send(ser, "delay 20", 3, until="scan pacing")

    print(f"Watching {len(targets)} PID(s) for {args.seconds}s. Ctrl+C to stop.")
    print("Move ONE thing at a time and watch which line moves with it.\n")

    last = {}
    t0 = time.time()
    rounds = 0
    try:
        while time.time() - t0 < args.seconds:
            rounds += 1
            for t in targets:
                ecu, req = t.split(".")
                txt = send(ser, f"scan {ecu} {ecu} {req}", 5, until="Done -")
                m = RX.search(txt)
                val = payload(m.group(1)) if m else None
                if val is None:
                    continue
                prev = last.get(t)
                if prev is None:
                    print(f"[{time.time()-t0:6.1f}s] {t} = {val}   (first read)")
                elif val != prev:
                    bits = ""
                    if len(prev) == len(val) == 2:
                        a, b = int(prev, 16), int(val, 16)
                        flips = [f"bit{i}:{(a>>i)&1}->{(b>>i)&1}"
                                 for i in range(8) if (a ^ b) >> i & 1]
                        bits = "  " + " ".join(flips)
                    print(f"[{time.time()-t0:6.1f}s] {t}  {prev} -> {val}{bits}")
                last[t] = val
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        ser.close()

    print(f"\n{rounds} round(s) over {time.time()-t0:.0f}s.")
    print("Final values:")
    for t in targets:
        print(f"  {t} = {last.get(t)}")


if __name__ == "__main__":
    sys.exit(main())
