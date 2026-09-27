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
Read and write the device's states over USB serial, for when the device is
not reachable over the network.

Uses the serial commands in loop() (src/main.cpp): `states` prints the current
states as one JSON line, `states {json}` replaces ALL of them. A write is
always read back and compared.

    get <out.json>          save the device's states
    put <in.json>           replace all states with the file
    add <patch.json>        merge: states in the patch replace the ones with the
                            same name, new ones go in before `bodyDoorByte`
    disable <name> [...]    set enabled=false on the named states
    reboot

Every write saves the previous states to states.backup-<time>.json first.
Opening the port resets the ESP32, so each command first waits until the
device answers `states` again.
"""
import argparse
import json
import os
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
CHUNK = 64


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def send(s, text, gap):
    data = (text + "\n").encode()
    for i in range(0, len(data), CHUNK):
        s.write(data[i:i + CHUNK])
        s.flush()
        time.sleep(gap)


def wait_for(s, prefixes, timeout):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += s.read(65536)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.decode(errors="replace").strip()
            for p in prefixes:
                i = line.find(p)
                if i >= 0:
                    return line[i:]
    return None


def read_states(s, gap, tries=40):
    for _ in range(tries):
        time.sleep(0.3)
        s.reset_input_buffer()
        send(s, "states", gap)
        line = wait_for(s, ("STATES ",), 6)
        if line and line[7:8] == "[":
            try:
                return json.loads(line[7:])
            except ValueError:
                pass
    sys.exit("device did not answer `states` - is the firmware new enough?")


def write_states(s, states, gap):
    current = read_states(s, gap)
    backup = os.path.join(HERE, time.strftime("states.backup-%Y%m%d-%H%M%S.json"))
    with open(backup, "w") as f:
        json.dump(current, f, indent=1, ensure_ascii=False)
    print(f"backed up {len(current)} states to {backup}")

    time.sleep(0.5)
    s.reset_input_buffer()
    send(s, "states " + json.dumps(states, ensure_ascii=False, separators=(",", ":")), gap)
    answer = wait_for(s, ("STATES OK", "STATES ERR", "CMD ERR"), 30)
    print(answer or "no answer")
    check = read_states(s, gap)
    if check != states:
        sys.exit(f"read back MISMATCH ({len(check)} vs {len(states)}); restore with: put {backup}")
    print(f"verified: {len(check)} states, {sum(x['enabled'] for x in check)} enabled")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    ap.add_argument("--gap", type=float, default=0.05, help="seconds between 64 byte chunks")
    ap.add_argument("cmd", choices=["get", "put", "add", "disable", "reboot"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()

    s = open_port(a.port)
    if a.cmd == "get":
        states = read_states(s, a.gap)
        with open(a.args[0], "w") as f:
            json.dump(states, f, indent=1, ensure_ascii=False)
        print(f"saved {len(states)} states to {a.args[0]}")
    elif a.cmd == "put":
        write_states(s, json.load(open(a.args[0])), a.gap)
    elif a.cmd == "add":
        patch = json.load(open(a.args[0]))
        states = read_states(s, a.gap)
        names = [x["name"] for x in states]
        insert_at = names.index("bodyDoorByte") if "bodyDoorByte" in names else len(states)
        for p in patch:
            if p["name"] in names:
                states[names.index(p["name"])] = p
                print(f"replace {p['name']}")
            else:
                states.insert(insert_at, p)
                names.insert(insert_at, p["name"])
                insert_at += 1
                print(f"add     {p['name']}")
        write_states(s, states, a.gap)
    elif a.cmd == "disable":
        states = read_states(s, a.gap)
        for x in states:
            if x["name"] in a.args:
                x["enabled"] = False
                print(f"disable {x['name']}")
        write_states(s, states, a.gap)
    elif a.cmd == "reboot":
        read_states(s, a.gap)
        send(s, "reboot", a.gap)
        print(wait_for(s, ("REBOOT",), 5) or "no answer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
