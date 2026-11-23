# OpenWebRX / OpenWebRX+ via iqfan

One dongle, shared with WebSDR, SvxLink and lab SDRs — OpenWebRX joins as an ordinary
rtl_tcp receiver ("RTL-SDR device (via rtl_tcp)"). No patches, no plugins.

## 1. Start iqfan

```
iqfan --daemon --log /tmp/iqfan.log --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
```

`-d` = your dongle serial, `-p <ppm>` if you have calibrated it. Center, rate and gain
are fixed here for **every** client; OpenWebRX cannot retune the dongle.

## 2. Add the receiver to config_webrx.py

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

## 3. Restart OpenWebRX and open the web UI

The "Shared RTL-SDR (iqfan)" device appears with the 40m profile; the waterfall spans
7.1 MHz ± 1.024 MHz while WebSDR and friends keep reading the same stream.

## Notes

- `center_freq` and `samp_rate` in every profile must equal iqfan's `-f` and `-s`.
  OpenWebRX sends rtl_tcp tuning commands; iqfan drains them, so a profile at another
  center would show the wrong spectrum. Need several centers? Run one iqfan per
  dongle/band, or keep all profiles at the same center with different `start_freq`.
- `rf_gain` and `ppm` in OpenWebRX are inert with iqfan. Set them on iqfan (`-g`, `-p`).
- `always-on: True` keeps the connector up even with no users, which suits a shared
  station where the dongle must never be released.
- 2048000 S/s is the clean choice. OpenWebRX's rtl_tcp source accepts 250k–3.2M, but
  2.88M drops samples on some USB setups (hardware ceiling, not iqfan).

## Troubleshooting

- **Device shows "failed" after an iqfan restart:** iqfan closes clients with RST when
  `rtl_sdr` dies/respawns, so the connector process exits. Toggle the device off/on in
  the web config (or restart OpenWebRX) to reconnect.
- **Waterfall at the wrong frequency:** profile `center_freq` ≠ iqfan `-f`.
- **Crackle or gaps at 2.88M:** drop to 2.048M (`-s 2048000` + matching `samp_rate`).
- **No data at all:** sanity-check the stream outside OpenWebRX:
  `nc 127.0.0.1 1235 | head -c 4` should print `RTL0`.

Back to the [README](../README.md).
