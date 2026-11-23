# iqfan — one dongle, every client.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Language: C](https://img.shields.io/badge/language-C-lightgrey.svg)](iqfan.c)
[![Platform: Linux | macOS](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-informational.svg)](#build)
[![Protocol: rtl_tcp](https://img.shields.io/badge/protocol-rtl__tcp-success.svg)](#works-with)

_Your RTL-SDR was never meant to be monogamous._

`iqfan` is a tiny (~300-line) C daemon that runs vanilla `rtl_sdr` and fan-outs its raw IQ to **any number of TCP clients at once**. It speaks the rtl_tcp greeting (`RTL0`) so SDR++ / SDRoxide / GQRX / OpenWebRX connect, while WebSDR and csdr/SvxLink read the very same stream.

One antenna, one dongle, every client. Zero dependencies beyond libc.

## Why this should exist

- `rtl_tcp` serves **one** client. `rtl_sdr` speaks **no protocol**. `nc` **hangs WebSDR** when the source dies.
- Real stations want one dongle feeding a WebSDR, a repeater (SvxLink/csdr) and a lab SDR **simultaneously**.
- Every existing tool solves only one slice. `iqfan` is the minimal glue: vanilla `rtl_sdr`'s robust USB handling + rtl_tcp protocol + multi-client + lifecycle management, in one small binary.

## What it fixes

| Problem | Without iqfan | With iqfan |
| --- | --- | --- |
| Single-client lock-in | rtl_tcp = 1 client | multi-client fan-out |
| Protocol mismatch | rtl_sdr = raw, SDR apps reject | `RTL0` greeting, rtl_tcp-compatible |
| Disconnect hang | nc keeps socket open, WebSDR stuck | RST on close + respawn -> reconnect |
| Orphan / stuck source | rtl_sdr survives, holds dongle | graceful -> force kill on exit |
| Port-restart race | "address in use", manual sleep | bind-retry until old instance dies |
| CPU spin | naive poll loop = 100% | conditional POLLOUT, ~1% CPU |

## Features

- Multi-client raw-IQ fan-out with bounded per-client buffers (slow clients dropped, never stall others).
- rtl_tcp `RTL0` greeting for SDR clients; raw IQ for WebSDR / csdr.
- Fixed center/gain/rate at launch = safe sharing (no client can retune the dongle).
- Clean disconnect (RST) + automatic respawn of `rtl_sdr`.
- `--daemon`, `--log`, `--help`, argument validation, bind-retry, force-kill.
- Passes rtl_sdr options through (`-f -s -g -d -p -b ...`); `-p` = ppm.

## Works with

| Client | Connect via | Example |
| --- | --- | --- |
| WebSDR | rtl_tcp mode: `device !rtlsdr <host>:1235` | [`examples/websdr.cfg`](examples/websdr.cfg) |
| OpenWebRX / OpenWebRX+ | source `type: "rtl_tcp"`, `remote: "<host>:1235"` | [`examples/openwebrx.md`](examples/openwebrx.md) |
| SDR++ / SDRoxide | rtl_tcp source -> `<host>:1235` | [`examples/sdroxide.md`](examples/sdroxide.md) |
| GQRX | device string `rtl_tcp=<host>:1235` | — |
| csdr / SvxLink | `nc <host> 1235 \| csdr convert_u8_f \| ...` | [`examples/svxlink-csdr.sh`](examples/svxlink-csdr.sh) |
| any other rtl_tcp client | point it at `<host>:1235` | — |

Tuning commands from clients are drained, not applied: the dongle stays where you launched it. That is the price — and the point — of safe sharing.

## 60-second quickstart

```
make
./iqfan --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
# WebSDR:    device !rtlsdr 127.0.0.1:1235
# OpenWebRX: type rtl_tcp, remote 127.0.0.1:1235 (see below)
# SDR++:     rtl_tcp source -> 127.0.0.1:1235
```

## Build

```
gcc -O2 -Wall -Wextra -o iqfan iqfan.c   # or: make
```

Needs `rtl_sdr` at runtime (the `rtl-sdr` package). Linux / macOS. No librtlsdr linking.

## Usage

```
iqfan --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
# background:
iqfan --daemon --log /tmp/iqfan.log --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
```

| Option | Meaning |
| --- | --- |
| `--port <p>` / `--port=<p>` | TCP listen port (required) |
| `--daemon` | fork to background |
| `--log <file>` | log stdout/stderr (default /dev/null) |
| `--help`, `-h` | help |
| `-f -s -g -d -p -b ...` | passed to rtl_sdr (`-p` = ppm) |

## OpenWebRX / OpenWebRX+

OpenWebRX ships an rtl_tcp connector ("RTL-SDR device (via rtl_tcp)"), so a shared iqfan stream is just another receiver in `config_webrx.py`:

```python
sdrs = {
    "iqfan": {
        "name": "Shared RTL-SDR (iqfan)",
        "type": "rtl_tcp",
        "remote": "127.0.0.1:1235",
        "always-on": True,
        "profiles": {
            "40m": {
                "name": "40m",
                "center_freq": 7100000,   # must equal iqfan -f
                "samp_rate": 2048000,    # must equal iqfan -s
                "start_freq": 7070000,
                "start_mod": "lsb",
            },
        },
    },
}
```

Rules of the road (iqfan fixes the dongle at launch):

- Profile `center_freq` / `samp_rate` must match iqfan's `-f` / `-s`; OpenWebRX's tuning commands are drained, not applied.
- `rf_gain` and `ppm` in OpenWebRX have no effect — set gain with `-g` and ppm with `-p` on the iqfan command line.
- Prefer 2048000 S/s. OpenWebRX accepts 250k–3.2M, but 2.88M crackles on some USB setups.
- If iqfan (or `rtl_sdr`) restarts, clients are closed with RST: the connector exits and the device shows as failed. Toggle the device off/on in the web config to reconnect.

Full walkthrough: [`examples/openwebrx.md`](examples/openwebrx.md).

## Limitations (honest)

- Fixed center/gain/rate: no remote tuning (that's the price of safe multi-client sharing).
- 2.88 MS/s may crackle on some hardware (USB ceiling); 2.048 MS/s is the clean choice.
- WebSDR's 512k axis quirks are WebSDR-side, not fixed here.

## Architecture

```
rtl_sdr (USB, librtlsdr) --stdout--> iqfan --TCP--> WebSDR
                                          |--TCP--> OpenWebRX / OpenWebRX+
                                          |--TCP--> csdr / SvxLink
                                          |--TCP--> SDR++ / SDRoxide / GQRX
```

## License

MIT — free for everyone. See `LICENSE`.
