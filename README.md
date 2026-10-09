# Pi-hole for Pebble

See your Pi-hole's status, pause blocking with one button and allow recently
blocked domains – right from your Pebble watch.

![Screenshots](docs/screenshots.png)

*Screenshots from the emulator with fictional sample data.*

[Deutsche Version](README.de.md) · [Changelog](CHANGELOG.md) · [Contributing](CONTRIBUTING.md)

If you enjoy the app, you can [buy me a coffee](https://buymeacoffee.com/michaelrunge) ☕ –
it's free and open source either way.

## Features

- **Status** – active or paused (with countdown), queries and blocked queries of the
  last 24 hours, active clients, size of the blocklist and when your lists were last updated
- **Pause** – 30 seconds, 5 or 15 minutes, 1 hour or indefinitely, and resume at any time.
  Hold SELECT to pause for your default duration.
- **Vibration when the pause ends** – even when the app is closed
- **Quick Launch** – put Pi-hole on a Quick Launch button and a long press pauses or resumes
  blocking right away
- **Recently blocked** – the latest blocked domains with client, time and reason.
  Allow a domain with one button (and undo it). For CNAME blocks the blocked target is allowed.
- **App glance** – the app list shows "Active · 12.7% blocked" or "Paused until 14:35"
- **Touch** on Pebble Time 2: tap the action bar or the status band
- **German and English**
- **Private** – the app only talks to *your* Pi-hole. Nothing else, no tracking.

## Requirements

- A Pebble Time 2
- The Pebble app on iPhone or Android
- **Pi-hole 6** (the new REST API) that your phone can reach

## Installation

1. Download `pihole-pebble.pbw` from the [latest release](../../releases/latest) and open
   it with the Pebble app, **or** build it yourself (see below).
2. In the Pebble app open **Pi-hole → Settings** and enter
   - the address of your Pi-hole, e.g. `http://192.168.1.2` (with `:port` if it isn't 80;
     `/admin` may be included)
   - an **app password** (recommended): Pi-hole → *Settings → Web interface / API* →
     switch to *Expert* → *Configure app password* → copy it and click *Enable new app password*.
     Your normal password works too, but not with two-factor authentication.
     Leave the field empty if your Pi-hole has no password.

Just curious? Enter `demo` as the address to try the app with sample data.

**HTTPS:** Pi-hole's built-in HTTPS uses a self-signed certificate, which the Pebble app
won't accept. Use `http://` in your home network, or a reverse proxy with a real certificate.

## Using it

| Where | Button | Action |
|---|---|---|
| Status | UP | reload |
| | SELECT | pause menu (or resume menu when paused) |
| | hold SELECT | pause for the default duration / resume |
| | DOWN | recently blocked |
| Recently blocked | SELECT | details |
| Details | SELECT | allow / undo allow |
| | UP / DOWN | scroll |

## Building it yourself

```bash
# Linux / macOS / Windows (WSL) – see https://developer.repebble.com/sdk/
uv tool install pebble-tool --python 3.13
pebble sdk install latest

pebble build
pebble install --emulator emery     # emulator
pebble install --cloudpebble        # watch (enable Dev Connect in the Pebble app)
```

In the emulator, open the settings with `pebble emu-app-config`.

## How it works

The watch never talks to the internet. PebbleKit JS on your phone logs in to the
[Pi-hole API](https://docs.pi-hole.net/api/) (`POST /api/auth`) and reuses the session
until it expires, so the app doesn't use up Pi-hole's limited API sessions. It then calls

- `GET /api/dns/blocking`, `POST /api/dns/blocking` – status and pausing
- `GET /api/stats/summary` – the numbers on the status screen
- `GET /api/queries?upstream=blocklist` – recently blocked queries, grouped by domain
- `GET/POST/DELETE /api/domains/allow/exact` – the allowlist

and sends prepared data to the watch via AppMessage. The watch side is plain C:
`status_window.c` (status screen), `pause_menu.c`, `blocked_window.c` (list and details),
`main.c` (messages, wakeup at the end of a pause, app glance).

**Security note:** an app password has full admin rights on your Pi-hole – Pi-hole has no
read-only passwords. It is stored only in the Pebble app on your phone.

## How this was made

This app was built with help from Claude (AI) and tested in the emulator and on
my own Pebble Time 2. Bug reports and pull requests are welcome.

## License

[MIT](LICENSE) © 2026 Michael Runge. Third-party code: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Pi-hole® is a registered trademark of Pi-hole LLC. This project is not affiliated with or
endorsed by Pi-hole LLC.
