# Toyota Enhanced PID Explorer — host tooling

Scripts that drive the on-device `ElmConsole` over USB CDC to find which bytes of
which enhanced PID carry door and light state on a **Toyota Corolla Altis 2009
1.8 MT**.

Everything here is **read only**. The firmware rejects any service that is not on
its allowlist, and these scripts only send `scan` / `raw` read requests.

## Prerequisites

```bash
pio run -e ESP32S3_N16R8_BLE_EXPLORER -t upload --upload-port /dev/cu.usbmodem101
```

Do **not** run `uploadfs` — it would wipe `settings.json` and `states.json` on the
device.

Python needs `pyserial`. The PlatformIO venv already has it:
`~/.pio-venv/bin/python`.

Close any serial monitor first — only one process can own the port.

## What was measured on the actual car (2026-09-12, engine running)

Diagnostic addresses that answer on CAN, found with `scan 700 7FF 0100`:

| TX id | RX id | Note |
| --- | --- | --- |
| `0x7A1` | `0x7A9` | non-engine ECU |
| `0x7B0` | `0x7B8` | non-engine ECU |
| `0x7C0` | `0x7C8` | non-engine ECU |
| `0x7E0` | `0x7E8` | engine ECU |
| `0x7DF` | `0x7E8` | functional broadcast |

Which ECU is which is **not established** — do not label them from a web table.

Supported enhanced PIDs, read from each ECU's own `21 00` / `21 20` bitmask
(not guessed):

| ECU | service 0x21 PIDs |
| --- | --- |
| `7A1` | 03 06 07 08 0A 0B 0C 15 21 22 23 24 25 26 27 28 29 |
| `7B0` | 03 08 1F 21 3D 3E 3F |
| `7C0` | 12 13 21 22 23 29 |

The vehicle answers Toyota service **`0x21`**. It explicitly rejects UDS `0x22`
with `7F 22 11` (serviceNotSupported), so `22xxxx` is the wrong service here.

## Capture and diff workflow

`capture.py` reads every PID in the table above and writes `snap_<label>.json`.

```bash
~/.pio-venv/bin/python capture.py doors_closed
# ... open the driver door, leave it open ...
~/.pio-venv/bin/python capture.py driver_open
~/.pio-venv/bin/python diff_snaps.py snap_doors_closed.json snap_driver_open.json
```

`diff_snaps.py` prints byte and bit level differences and a `candidate:` line for
every bit that moved.

## Noise floor — read this before trusting a candidate

Two snapshots taken back to back with **nothing touched** already differ in
**9 PIDs**. `snap_baseline.json` and `snap_baseline2.json` in this directory are
that pair; diff them to see the drift:

```bash
~/.pio-venv/bin/python diff_snaps.py snap_baseline.json snap_baseline2.json
```

Known drifters: `7A1.2115` (swings widely), `7A1.2123` bit 0, `7C0.2113` bits 0-1,
`7C0.2123` bits 0-1, plus the analog-looking `7A1.2106/210A/210B/210C`.

So a single diff is **not** evidence. A candidate only counts when the same bit
flips the same way across repeated open/close cycles, and stays put when nothing
is touched. Capture at least three cycles per state before believing anything.

## Suggested capture sequence

Ignition on, engine may idle. For each step take one snapshot:

```text
all_closed_1   everything shut, lights off
driver_open    driver door open
all_closed_2   shut again
pass_open      front passenger door open
all_closed_3
rear_l_open    rear left door open
all_closed_4
tail_on        light switch in tail position
head_on        light switch in head position
high_beam      high beam on
lights_off
```

Then diff each state against the `all_closed_*` around it, and keep only bits
that reproduce.

## Passive alternative

The firmware also has `mon [sec]`, a passive `ATMA` monitor. The bus carries
broadcast frames (`0B0 0B2 0B4 0BA 224 232 262 2C1 2C4 38A 442 620` seen in 12 s).
If a door or light state is broadcast there, no diagnostic request is needed at
all. That path needs someone at the car toggling things while `mon` runs.

## Noise mask (added after measuring the drift)

`diff_snaps.py` accepts extra snapshots of the **same state as A**, and uses them
to build a mask of bits that move on their own. Masked bits print as
`[noise, ignored]`; anything left prints as `*** CANDIDATE`.

```bash
# mask built from every closed-state snapshot we have
~/.pio-venv/bin/python diff_snaps.py \
    snap_all_closed_1.json snap_driver_open_1.json \
    snap_all_closed_2.json snap_baseline.json snap_baseline2.json
```

More same-state references = fewer false candidates. With only two references the
mask is still leaky, so a surviving candidate must still reproduce across repeated
open/close cycles before it counts.

## Live diff — the fast way to map switches

`mon_capture.py` + `mon_diff.py` costs roughly a minute per vehicle state. To
sweep a list of switches that is far too slow, so the firmware also has a
`live` mode: it keeps the last payload per CAN id and prints a line the moment
one moves.

```bash
~/.pio-venv/bin/python live.py --seconds 300 --log sweep.txt
```

Ignition at least ON. Toggle **one** thing, read the line it produced, then the
next. Output looks like:

```text
[LIVE] learnt 39 id(s), muted 5 chatty. Go ahead.
[LIVE] 620 t+42s
       byte 5: 40 -> 60  bit5:0->1
```

Ids that keep changing by themselves (wheel speeds, counters) are learnt during
the first 8 seconds and muted, so they do not drown the signal. That is learnt
rather than hard coded because what counts as chatty differs between parked and
driving.

If a switch produces nothing, it may sit in a muted id - re-run and toggle it
during the *learning* phase to see whether that id was moving anyway.

### Worth sweeping (all doable parked)

trunk, hood, door lock/unlock, brake pedal, handbrake, indicator left,
indicator right, hazard, horn, seatbelt, reverse gear, windows, wipers,
A/C and fan, rear defogger.

`0x620` byte 5 still has **bit 0 and bit 1 unassigned**, and the whole `6xx`
block family (`610 611 620 621 622 624 630 638`) is unexplored - byte 0 of each
is a block index, so they are very likely more body status.

### Needs the car moving — safety

`0B0`/`0B2` (per wheel speed), `2C1`/`260`/`2C4` (steering / brake / accel)
only vary while driving. **Do not operate a laptop while driving.** Use a second
person, or log to file and analyse afterwards.
