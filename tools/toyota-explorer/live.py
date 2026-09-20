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
Run the firmware's live bus diff and show changes as they happen.

Capture-and-compare costs about a minute per vehicle state, which is far too
slow to sweep a list of switches. This streams the result instead: toggle one
thing, read the line it produced, move on.

    live.py --seconds 300 --log sweep.txt

Ignition must be at least ON, or the bus is asleep and nothing is broadcast.
Nothing is ever sent to the vehicle - the firmware only listens.
"""
import argparse
import sys
import time

import serial

PORT, BAUD = "/dev/cu.usbmodem101", 115200

NOISE_PREFIX = (
    "OBD PID", "MQTT state", "Send ", "...done",
    "I NimBLE", "W NimBLE", "VLinkBLEStream", "Bluetooth LE",
)


def wait_ready(ser, timeout=120):
    """Wait passively for the link. Sending anything here starves the
    firmware's reconnect and corrupts the next command - see README."""
    print("waiting for the BLE link (passive)...", end="", flush=True)
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n).decode("utf-8", errors="replace")
            if "Connected to ELM327" in buf:
                print(" ready.\n")
                return True
            buf = buf[-4000:]
        else:
            time.sleep(0.05)
    print(" TIMEOUT")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--log", default=None, help="also append the session to this file")
    ap.add_argument("--mute-limit", type=int, default=3,
                    help="changes during settle above which an id is muted; "
                         "0 mutes nothing (use when a signal may hide in a busy frame)")
    args = ap.parse_args()

    log = open(args.log, "a") if args.log else None

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = PORT, BAUD, 1
    ser.dtr = ser.rts = False
    ser.open()

    if not wait_ready(ser):
        print("adapter never came up - is the ignition on?")
        return 1

    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.write(f"live {args.seconds} {args.mute_limit}\n".encode())
    ser.flush()

    print("=" * 56)
    print("Toggle ONE thing, wait for its line, then the next.")
    print("Suggested order (all doable parked):")
    print("  trunk, hood, lock/unlock, brake pedal, handbrake,")
    print("  indicator L, indicator R, hazard, horn, seatbelt,")
    print("  reverse gear, window, wipers, A/C, rear defogger")
    print("=" * 56)

    end = time.time() + args.seconds + 20
    buf = ""
    try:
        while time.time() < end:
            n = ser.in_waiting
            if not n:
                time.sleep(0.02)
                continue
            buf += ser.read(n).decode("utf-8", errors="replace")
            *lines, buf = buf.split("\n")
            for line in lines:
                line = line.strip()
                if not line or line.startswith(NOISE_PREFIX):
                    continue
                print(line, flush=True)
                if log:
                    log.write(line + "\n")
                    log.flush()
                if line.startswith("Done -") or line.startswith("STOPPED"):
                    end = 0
    except KeyboardInterrupt:
        print("\ninterrupted - stopping the monitor...")
        ser.write(b"\n")
        time.sleep(1)
    finally:
        ser.close()
        if log:
            log.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
