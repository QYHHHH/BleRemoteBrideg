# MiRemoteBridge

**English** | [简体中文](README.zh-CN.md)

> ESP32-C3 dual-role BLE bridge firmware that turns a **Bluetooth remote** into a standard Bluetooth keyboard + media-control device your PC recognizes.

🔗 Project site (flashing / Wi-Fi provisioning / key remapping all happen here): **https://qyhhhh.github.io/BleRemoteBrideg/**

<img src="docs/images/architecture.png" alt="How MiRemoteBridge works" width="720">

## What is this

An open-source firmware running on an ESP32-C3 dev board that makes your PC treat a remote **as an ordinary Bluetooth keyboard**:

- No drivers, companion apps, or background services
- No changes to Windows or the registry
- No administrator privileges
- Won't trip game anti-cheat, won't get flagged by antivirus
- Key presses are forwarded entirely locally — zero telemetry

**Use cases**:

- You have a spare Bluetooth remote lying around, or want the cheapest set-top-box Bluetooth remote, to control your PC
- You don't want to install any software on your PC, don't want to fight with drivers, and want to stay clear of game anti-cheat checks

## The problem it solves

Bluetooth remotes on the market fall into two camps, and both have pain points:

| Type | Pain point |
|---|---|
| **Xiaomi remote (RC003)** | Stuffs **vendor-private codes** for volume, back, and power into its HID reports — Windows simply drops them |
| **Standard HOGP remotes** (e.g. those bundled with China Mobile / China Telecom boxes) | Can't be used on a PC at all |

This firmware connects to the remote as a Central, translates or passes through whatever codes it sends into standard HID reports, then forwards them to your PC as a "Bluetooth keyboard" — **all your PC sees is a perfectly ordinary Bluetooth keyboard**.

## Advantages

| Advantage | Details |
|---|---|
| **Dirt cheap** | An ESP32-C3 dev board is about **¥10** + a standard BLE remote **¥5–15** = **¥15–25** for the whole setup |
| **Fully driverless** | Your PC sees a standard Bluetooth keyboard + media-control device; plug and play on Windows / Linux / macOS |
| **No software to install** | Want to remap a key? Just open the board's page in a browser — no client, no driver |
| **No system changes** | Doesn't touch the Windows registry, system drivers, or group policy; uninstalling means unplugging the USB cable |
| **Anti-cheat friendly** | A pure HID device — Riot Vanguard / EAC / BattlEye and other mainstream anti-cheat let it through |
| **Antivirus friendly** | No background process, no autostart entry, no network access (unless you open the config page yourself) — nothing suspicious to flag |
| **No admin rights needed** | Pairing goes through the standard Windows Bluetooth settings; flashing goes over USB serial — a regular user account is enough |
| **Best-in-class keyboard compatibility** | Emits standard HID Keyboard + Consumer Control reports, universal across PC platforms |
| **Two remotes supported out of the box** | Xiaomi RC003 (13 keys + battery) and China Mobile `CMCC_Voice_Remote` are verified end to end; any other standard BLE remote can be onboarded through learning mode |

## Compatibility

| Role | Device | Status |
| --- | --- | --- |
| Remote (upstream) | Xiaomi Bluetooth Remote 2 Pro (RC003) | ✅ Verified on real hardware (13 keys + battery) |
| Remote (upstream) | China Mobile voice remote (`CMCC_Voice_Remote`) | ✅ Verified end to end on real hardware, **some physical keys don't respond** (see [`docs/CMCC-BLE-REMOTE.md`](docs/CMCC-BLE-REMOTE.md)) |
| Host (downstream) | Windows 10 / 11 | ✅ Verified on real hardware (keyboard + media keys + Bluetooth off/on + multi-host switching) |
| Host (downstream) | Linux / macOS | Not verified; standard Bluetooth HID, should work in principle — feedback welcome |
| Host (downstream) | iPhone / iPad / iOS | ✅ Verified on real hardware (keyboard + media keys + multi-host switching, see [`docs/PAIRING.md`](docs/PAIRING.md)) |
| Remote (upstream) | Other standard BLE remotes | Use a **learning slot**; standard HID key names are recognized automatically, **no code changes required**. Only CMCC has been verified key by key |

## Quick start

Flashing, Wi-Fi setup, and key remapping all happen in the browser — **no software to install, no serial commands**.

1. **Open the project site**: https://qyhhhh.github.io/BleRemoteBrideg/ (linked at the top of this page)
2. **Plug in the board**: connect the ESP32-C3 dev board to your PC with a USB cable
3. **Flash from the browser**: click "Install" on the site; the browser picks the serial port and flashes the firmware
   (powered by [ESP Web Tools](https://esphome.github.io/esp-web-tools/) on top of Web Serial —
   **no client to install**, just desktop Chrome / Edge / Opera)
4. **Configure Wi-Fi**: once flashing completes, the page walks you through entering your home Wi-Fi SSID and password
5. **Open the config page**: after the board joins Wi-Fi, the page redirects automatically to the board's own config page
6. **Done**: press a key on the remote and your PC responds; to remap a key, click its card on the config page

> **Serial commands** (`status` / `scan` / `connect`, etc.) are **for debugging only** — everything a user
> needs is done in the browser. Command reference: [`docs/SERIAL-CONSOLE.md`](docs/SERIAL-CONSOLE.md);
> troubleshooting: [`docs/PAIRING.md`](docs/PAIRING.md).

## Documentation

### For users

- [`docs/PAIRING.md`](docs/PAIRING.md) — upstream / downstream pairing steps, serial commands, troubleshooting
- [`docs/RECOVERY.md`](docs/RECOVERY.md) — clearing bonds, failure-recovery matrix, stuck-key protection
- [`docs/KEYMAP.md`](docs/KEYMAP.md) — full 13-key table, configurable modes, HID descriptor and service layout
- [`docs/WEB-UI.md`](docs/WEB-UI.md) — using the config page, access window, security boundaries, heap watermarks
- [`docs/SERIAL-CONSOLE.md`](docs/SERIAL-CONSOLE.md) — serial command reference, monitoring pitfalls

### For developers

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — code structure, key design decisions, module boundaries
- [`docs/TESTING.md`](docs/TESTING.md) — four-layer verification status, on-hardware bring-up logs, acceptance checklist
- [`docs/PITFALLS.md`](docs/PITFALLS.md) — hard-won gotchas (the most valuable part of open-sourcing this)
- [`docs/RELEASING.md`](docs/RELEASING.md) — firmware release process, artifact list, naming contract
- [`docs/AUTH.md`](docs/AUTH.md) — history of the auth design (why it became a 30-minute window)

### Research archive ("why we didn't build it")

- [`docs/CMCC-BLE-REMOTE.md`](docs/CMCC-BLE-REMOTE.md) — investigating the China Mobile remote's MIC verification failure, and where it stands
- [`docs/MIC-AUDIO-FEASIBILITY.md`](docs/MIC-AUDIO-FEASIBILITY.md) — four architectures evaluated for microphone audio (conclusion: not doing it)
- [`docs/HANDOFF-websocket-debug.md`](docs/HANDOFF-websocket-debug.md) — WebSocket debugging post-mortem
- [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md) — third-party references and licensing conclusions

## Acknowledgements

- [GetSayAll/remote-mic-app-windows](https://github.com/GetSayAll/remote-mic-app-windows) (GPL-3.0) —
  this project's web config UI takes inspiration from its key-mapping page for **layout and interaction design**
  (key cards arranged around the remote, one config slot per key). No source code or asset files were copied;
  the page is an independent implementation. Its published notes on RC003 key codes and pairing behavior
  also helped cross-check the key table.
- [cuicui-V5/RemoteMapper-ESP32](https://github.com/cuicui-V5/RemoteMapper-ESP32) —
  the first similar project we looked at; its record of the raw RC003 key codes got this project off the ground
  (4 of those codes were later corrected against real hardware, see [`docs/KEYMAP.md`](docs/KEYMAP.md) §1).

## License

Released under **GPL-3.0-or-later**, see [`LICENSE`](LICENSE).
Copyleft was chosen so that every improvement built on this project stays open source too.
Third-party references and asset boundaries: [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md).
