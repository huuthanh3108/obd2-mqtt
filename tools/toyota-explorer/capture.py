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
"""Capture a full snapshot of every supported enhanced PID on the 3 non-engine ECUs."""
import json, re, sys, time, serial
PORT, BAUD = "/dev/cu.usbmodem101", 115200
RX = re.compile(r"RX\s+([0-9A-F]+)\s")
PLAN = {
    "7A1": ["03","06","07","08","0A","0B","0C","15","21","22","23","24","25","26","27","28","29"],
    "7B0": ["03","08","1F","21","3D","3E","3F"],
    "7C0": ["12","13","21","22","23","29"],
}
def send(ser, cmd, wait, until=None):
    """Send a command and stop reading as soon as `until` shows up."""
    ser.reset_input_buffer(); ser.write((cmd+"\n").encode()); ser.flush()
    end, buf = time.time()+wait, b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            if until and until in buf.decode("utf-8", errors="replace"):
                break
        else:
            time.sleep(0.02)
    return buf.decode("utf-8", errors="replace")


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
label = sys.argv[1] if len(sys.argv) > 1 else "baseline"
# Do not toggle DTR/RTS: that resets the board and costs a 12s reboot every run.
ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = PORT, BAUD, 1
ser.dtr = ser.rts = False
ser.open()
time.sleep(1.0)
ser.reset_input_buffer()
if not wait_ready(ser):
    raise SystemExit('adapter never came up - is the ignition on?')
send(ser, "delay 60", 3, until="scan pacing")
snap = {}
for ecu, pids in PLAN.items():
    for p in pids:
        val = None
        for attempt in range(3):
            txt = send(ser, f"scan {ecu} {ecu} 21{p}", 6, until="Done -")
            m = RX.search(txt)
            if m:
                val = m.group(1)
                break
            time.sleep(0.4)
        snap[f"{ecu}.21{p}"] = val
        print(f"{ecu}.21{p} = {snap[f'{ecu}.21{p}']}"); sys.stdout.flush()
ser.close()
with open(f"snap_{label}.json", "w") as f:
    json.dump(snap, f, indent=1)
print(f"\nsaved snap_{label}.json  ({sum(1 for v in snap.values() if v)}/{len(snap)} answered)")
