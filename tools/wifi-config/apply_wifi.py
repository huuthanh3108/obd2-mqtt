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
Push an ordered Wi-Fi fallback list to the device.

The list lives in settings.json on the device, so it survives every firmware
flash. Only `uploadfs` would wipe it - and that is exactly why the list is not
kept in data/.

    cp wifi.example.json wifi.json     # then fill in the passwords
    ./apply_wifi.py --host 172.20.10.10

Reads the device's current settings, replaces only wifi.networks, and writes
them back, so MQTT and OBD settings are untouched. A backup of the settings as
they were is written next to this script before anything is sent.
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def fetch(url, timeout):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode())


def put(url, payload, timeout):
    # the firmware is built with ARDUINOJSON_DECODE_UNICODE=0, so a \uXXXX
    # escape would be stored verbatim - send raw UTF-8 instead.
    data = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="PUT",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="172.20.10.10")
    ap.add_argument("--file", default=os.path.join(HERE, "wifi.json"))
    ap.add_argument("--timeout", type=int, default=40)
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would be sent and stop")
    args = ap.parse_args()

    if not os.path.exists(args.file):
        print(f"missing {args.file}")
        print("cp wifi.example.json wifi.json and fill it in")
        return 1

    cfg = json.load(open(args.file))
    nets = [n for n in cfg.get("networks", []) if n.get("ssid")]
    if not nets:
        print("no networks with an ssid in the file")
        return 1
    if len(nets) > 5:
        print(f"{len(nets)} networks, firmware keeps at most 5 (WIFI_MAX_NETWORKS)")
        return 1

    base = f"http://{args.host}/api/settings"
    try:
        current = fetch(base, args.timeout)
    except (urllib.error.URLError, TimeoutError) as e:
        print(f"cannot reach {base}: {e}")
        print("is the device on the same network and powered?")
        return 1

    backup = os.path.join(HERE, "settings.backup.json")
    with open(backup, "w") as f:
        json.dump(current, f, indent=1)
    print(f"backed up current settings to {backup}")

    current.setdefault("wifi", {})
    current["wifi"]["networks"] = [
        {"ssid": n["ssid"], "password": n.get("password", "")} for n in nets
    ]

    print("order to be written (0 is tried first):")
    for i, n in enumerate(current["wifi"]["networks"]):
        shown = "<set>" if n["password"] else "<open>"
        print(f"  {i}. {n['ssid']}   password {shown}")

    if args.dry_run:
        print("dry run, nothing sent")
        return 0

    status = put(base, current, args.timeout)
    print(f"PUT {status}")
    if status != 200:
        print(f"device refused the write; restore with {backup} if needed")
        return 1

    check = fetch(base, args.timeout)
    got = [n.get("ssid") for n in check.get("wifi", {}).get("networks", [])]
    print("device now reports:", got)
    print("reboot the device (or wait for the next reconnect) to apply.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
