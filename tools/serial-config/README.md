# Serial config + in-car test

Tools for when the device is not reachable over the network. Both use
`~/.pio-venv/bin/python` (it has `pyserial`).

| Script | What it does |
|---|---|
| `serial_states.py` | `get` / `put` / `add` / `disable` / `reboot` the device's states over USB. Every write backs up first and reads back to verify |
| `mqtt_watch.py` | What the device really publishes: live count and value distribution per state, retained values listed apart |
| `test_states_<n>_<group>.json` | States to add for one test group (below) |

Opening the USB port resets the ESP32. Close any serial monitor first.

## Test plan by group (prepared 2026-09-27)

Groups are ordered by how much they matter for the health of the car. Do one
group per session and clean up after it: every PID with its own header costs
three adapter commands per read and slows everything else down.

Background and sources: README.md, "Hướng giải quyết cho phần chưa làm".
All commands run from `tools/serial-config/` after `alias py=~/.pio-venv/bin/python`. Adding states needs the Mac on USB; watching only needs
the broker. Home Assistant history graphs work just as well as `mqtt_watch.py`.

| # | Group | Needs | Adds states | Time |
|---|---|---|---|---|
| 1 | **Engine and battery health** — verify what is already published | engine cold start, then warm idle | none | 20-30 min |
| 2 | Fuel level from the meter ECU | ignition ON, USB | `test_states_2_fuel.json` | 5 min |
| 3 | Lights from the meter ECU | ignition ON, USB, someone at the switch | `test_states_3_lights.json` | 15 min |
| 4 | Body switches: lock, trunk, hood, brake, handbrake, indicators, … | ignition ON, EXPLORER firmware, someone in the car | none (`live.py`) | 20 min |
| 5 | Signals that only move while driving: wheel speeds, steering, brake, throttle | a second person driving, log to file | none | 1 drive |

### Group 1 — engine and battery health (nothing to add)

These states are already published; the point is to check that each one
behaves like a healthy engine **over one warm-up**, and to note the baseline for
the automations in README.md. Start with a **cold** engine (parked for hours):

```bash
py mqtt_watch.py --seconds 1500 milState numDTCs fuelSystemStatus engineCoolantTemp \
  batteryVoltage controlModuleVoltage shortTermFuelTrimBank1 longTermFuelTrimBank1 \
  o2SensorB1S1Lambda o2SensorB1S2Voltage catalystTempBank1Sensor1 catalystTempBank1Sensor2 rpm
```

| Check | Healthy | Write down |
|---|---|---|
| `milState` / `numDTCs` | `off` / `0` | |
| `batteryVoltage` before start (ignition ON) | ≥ 12.4 V | resting voltage |
| `batteryVoltage` idling | 13.5-14.5 V (charging) | charging voltage |
| `fuelSystemStatus` | `1` (open loop, cold) → `2` (closed loop) within a few minutes | minutes to closed loop |
| `engineCoolantTemp` | climbs steadily, levels off ~85-95 °C | minutes to 80 °C, idle plateau |
| `rpm` | higher when cold, settles ~650-750 when warm | warm idle rpm |
| `longTermFuelTrimBank1` (warm) | within ±5 % (±10 % still acceptable) | LTFT at warm idle |
| `shortTermFuelTrimBank1` (warm) | swings around 0 | |
| `o2SensorB1S1Lambda` (closed loop) | ~0.97-1.03 | |
| `o2SensorB1S2Voltage` (warm) | fairly steady, ~0.5-0.8 V; a copy of the upstream swings would point at a worn catalyst | |
| `catalystTempBank1Sensor2` | rises after sensor 1 and stays below it | |

These ranges are general petrol-engine references, not Corolla specifications.
The baseline you write down is what the automations should be tuned to.

### ~~Group 1b — transmission fluid temperature~~

Dropped 2026-09-27: the car has a **manual** gearbox, so there is no ATF and no
transmission ECU (`7E1` never answered). `test_states_1_health.json` is kept only
as a record of what was tried.

### Group 2 — fuel level

`fuelLevelMeter`: header `7C0`, request `21 29`, value = A/2. Note the needle first.

```bash
py serial_states.py add test_states_2_fuel.json
py mqtt_watch.py --seconds 180 fuelLevelMeter
```

At 1/4 tank: about **14** means litres (tank ~55 L), about **25** means percent.
Then confirm over days: it must **jump after refuelling** and **fall steadily**
while driving. If it works: `py serial_states.py disable fuelCandidate610b3
fuelCandidate611b2 fuelCandidate611b3 fuelCandidate624b2 fuelCandidate624b3
fuelCandidate624b4 fuelCandidate638b2`.

### Group 3 — lights

`meterPid2112/2113/2121/2122/2123`: raw meter PIDs every 5 s. Hold each switch
position **30 s**, go round **3 times**:

```text
off -> tail -> head -> off   (x3)
```

```bash
py serial_states.py add test_states_3_lights.json
py mqtt_watch.py --seconds 400 meterPid2112 meterPid2113 meterPid2121 meterPid2122 meterPid2123 lightsOn headlightsOn
```

A PID counts only when it moves the same way on **all three** rounds and returns
at every `off`. `2113` bits 0-1 and `2123` bits 0-1 drift on their own - ignore
them. Afterwards: `py serial_states.py disable meterPid2112 meterPid2113
meterPid2121 meterPid2122 meterPid2123`, keeping only the one that works.

### Group 4 — body switches

Needs the EXPLORER firmware (`pio run -e ESP32S3_N16R8_BLE_EXPLORER -t upload`)
and `tools/toyota-explorer/live.py`. Toggle **one** thing at a time and wait for
its line; see `tools/toyota-explorer/README.md`. Order: door lock/unlock
(re-run with `--mute-limit 0`), trunk, hood, brake pedal, handbrake, indicator
left/right, hazard, seatbelt, reverse. Flash production again afterwards.

### Group 5 — while driving

Wheel speeds (`0B0`/`0B2`), steering / brake / throttle (`2C1`/`260`/`2C4`).
**Nobody touches the laptop while driving**: start `mon_freq.py` with a long
`--seconds` before leaving, let a passenger note the manoeuvres with times, and
analyse afterwards.

### Restore

Every write backed up the previous states:
`py serial_states.py put states.backup-<time>.json`.
