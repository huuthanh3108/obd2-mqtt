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
Watch what the device publishes, with no MQTT library (MQTT 3.1.1 over TLS).

    mqtt_watch.py --seconds 60 fuelLevelMeter atfTemperature meterPid2113

Prints, per state, how many live messages arrived and each distinct value
with its count - a value that only shows up once is not the same thing as a
steady reading. RETAINED values are listed separately: they were sitting on
the broker and say nothing about what the device reads now.

Host, port, user and password come from data/settings.json.
"""
import argparse
import collections
import json
import os
import select
import socket
import ssl
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def s(x):
    b = x.encode()
    return struct.pack("!H", len(b)) + b


def remaining(n):
    out = b""
    while True:
        d = n % 128
        n //= 128
        out += bytes([d | (0x80 if n else 0)])
        if not n:
            return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("names", nargs="*", help="state names; none = everything")
    a = ap.parse_args()

    cfg = json.load(open(os.path.join(ROOT, "data", "settings.json")))["mqtt"]
    raw = socket.create_connection((cfg["hostname"], cfg["port"]), 10)
    c = ssl.create_default_context(cafile="/etc/ssl/cert.pem").wrap_socket(raw, server_hostname=cfg["hostname"]) \
        if cfg.get("secure") else raw

    body = s("MQTT") + bytes([4, 0xC2]) + struct.pack("!H", 60) + s(f"watch-{int(time.time())}") \
        + s(cfg["username"]) + s(cfg["password"])
    c.sendall(b"\x10" + remaining(len(body)) + body)
    sub = struct.pack("!H", 1) + s("obd2mqtt/#") + b"\x00"
    c.sendall(b"\x82" + remaining(len(sub)) + sub)

    live = collections.defaultdict(collections.Counter)
    retained = {}
    buf = b""
    end = time.time() + a.seconds
    next_ping = time.time() + 30
    while time.time() < end:
        # Keep-alive is 60s; without a PINGREQ the broker drops us after ~90s.
        if time.time() >= next_ping:
            c.sendall(b"\xc0\x00")
            next_ping = time.time() + 30
        r, _, _ = select.select([c], [], [], 0.5)
        if r:
            d = c.recv(65536)
            if not d:
                print("broker closed the connection")
                break
            buf += d
        while len(buf) >= 2:
            i, mult, n = 1, 1, 0
            while i < len(buf):
                b = buf[i]
                n += (b & 127) * mult
                mult *= 128
                i += 1
                if not b & 128:
                    break
            if len(buf) < i + n:
                break
            h, pkt, buf = buf[0], buf[i:i + n], buf[i + n:]
            if h >> 4 == 2 and pkt[1] != 0:
                sys.exit(f"CONNACK refused, rc={pkt[1]}")
            if h >> 4 != 3:
                continue
            tl = struct.unpack("!H", pkt[:2])[0]
            topic = pkt[2:2 + tl].decode()
            payload = pkt[2 + tl + (2 if h & 6 else 0):].decode(errors="replace")
            name = topic.split("_", 1)[1] if "_" in topic else topic
            if a.names and name not in a.names:
                continue
            if h & 1:
                retained[name] = payload
            else:
                live[name][payload] += 1

    for name in (a.names or sorted(set(live) | set(retained), key=str.lower)):
        counts = live.get(name)
        values = "  ".join(f"{v!r}x{n}" for v, n in counts.most_common(8)) if counts else "-"
        print(f"{name:28} live={sum(counts.values()) if counts else 0:4}  {values}"
              + (f"   [retained {retained[name]!r}]" if name in retained else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
