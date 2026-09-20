# Ordered Wi-Fi fallback

The device tries the configured networks **strictly top to bottom**. Entry 0
wins even when a later entry has a stronger signal — that is the point: the
phone hotspot should beat the home network when both are in range.

Deliberately not `WiFiMulti`, which picks by RSSI and would do the opposite.

## Where the list lives

In `settings.json` **on the device**, not in the repo and not in `data/`.

- Survives every `pio run -t upload` (firmware flash does not touch the filesystem)
- Only `uploadfs` would wipe it, and that command also destroys the MQTT and OBD
  settings, so it should not be used for routine config anyway
- Passwords never enter git

## Usage

```bash
cd tools/wifi-config
cp wifi.example.json wifi.json      # gitignored
$EDITOR wifi.json                   # fill in ssid + password, in priority order
~/.pio-venv/bin/python apply_wifi.py --host 172.20.10.10 --dry-run
~/.pio-venv/bin/python apply_wifi.py --host 172.20.10.10
```

The script reads the device's current settings, replaces **only**
`wifi.networks`, and writes them back — MQTT and OBD settings are left alone. It
saves `settings.backup.json` before sending anything.

Reboot the device, or wait for the next reconnect, to apply.

## Limits

- At most **5** networks (`WIFI_MAX_NETWORKS` in `src/settings.h`)
- Each attempt costs one `WIFI_CONNECT_TIMEOUT_MS` before moving on, so a long
  list of unreachable networks delays startup. Put the likely one first.
- The legacy single `wifi.ssid` / `wifi.password` still works: when
  `networks` is empty it is used as the only entry, so an existing
  `settings.json` keeps working untouched.

## Reaching the device

Over the hotspot/home network at its STA address (`/api/wifi` reports it), or by
joining the device's own AP `OBD2-MQTT-<mac>` (password `obd2mqtt`) and using
`http://192.168.4.1`. The AP route works even when no configured network is
reachable — which is the situation you are most likely fixing.
