# iqfan — one dongle, every client.

*Your RTL-SDR was never meant to be monogamous.*

`iqfan` is a tiny (~300-line) C daemon that runs vanilla `rtl_sdr` and fan-outs its raw IQ to
**any number of TCP clients at once**. It speaks the rtl_tcp greeting (`RTL0`) so SDR++ /
SDRoxide / GQRX connect, while WebSDR and csdr/SvxLink read the very same stream.

One antenna, one dongle, every client. Zero dependencies beyond libc.

## Why this should exist

- `rtl_tcp` serves **one** client. `rtl_sdr` speaks **no protocol**. `nc` **hangs WebSDR** when the source dies.
- Real stations want one dongle feeding a WebSDR, a repeater (SvxLink/csdr) and a lab SDR **simultaneously**.
- Every existing tool solves only one slice. `iqfan` is the minimal glue: vanilla `rtl_sdr`'s robust USB
  handling + rtl_tcp protocol + multi-client + lifecycle management, in one small binary.

## What it fixes

| Problem | Without iqfan | With iqfan |
|---|---|---|
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

## Build

```sh
gcc -O2 -Wall -Wextra -o iqfan iqfan.c   # or: make
```
Needs `rtl_sdr` at runtime (the `rtl-sdr` package). Linux / macOS. No librtlsdr linking.

## Usage

```sh
iqfan --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
# background:
iqfan --daemon --log /tmp/iqfan.log --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
```

| Option | Meaning |
|---|---|
| `--port <p>` / `--port=<p>` | TCP listen port (required) |
| `--daemon` | fork to background |
| `--log <file>` | log stdout/stderr (default /dev/null) |
| `--help`, `-h` | help |
| `-f -s -g -d -p -b ...` | passed to rtl_sdr (`-p` = ppm) |

## Examples

- **WebSDR:** `device !rtlsdr 127.0.0.1:1235` + matching `samplerate`/`centerfreq` (see `examples/websdr.cfg`).
- **csdr / SvxLink:** `nc 127.0.0.1 1235 | csdr convert_u8_f | ...` (see `examples/svxlink-csdr.sh`).
- **SDRoxide / SDR++:** connect rtl_tcp to `<host>:1235` (fixed center; see `examples/sdroxide.md`).

## Limitations (honest)

- Fixed center/gain/rate: no remote tuning (that's the price of safe multi-client sharing).
- 2.88 MS/s may crackle on some hardware (USB ceiling); 2.048 MS/s is the clean choice.
- WebSDR's 512k axis quirks are WebSDR-side, not fixed here.

## Architecture

```
rtl_sdr (USB, librtlsdr) --stdout--> iqfan --TCP--> WebSDR
                                          |--TCP--> csdr / SvxLink
                                          |--TCP--> SDR++ / SDRoxide
```

## License

MIT — free for everyone. See `LICENSE`.
