# Contributing

Thanks for helping! Bug reports and ideas are just as welcome as code.
German or English – both are fine.

## Reporting bugs and ideas

Use the [issue templates](../../issues/new/choose). For bugs, please include your
watch model, phone (iOS/Android), app version and Pi-hole version (Settings → System).
Logs help a lot: connect the watch (Dev Connect) and run `pebble logs --cloudpebble`.

**Never post your password, app password or server address** in an issue.

## Development setup

```bash
uv tool install pebble-tool --python 3.13
pebble sdk install latest
pebble build
pebble install --emulator emery
pebble emu-app-config        # opens the settings page for the emulator
```

Tip: enter `demo` as address to work without a Pi-hole.

## Things to know

- **Protocol:** message types and commands are defined twice – in `src/c/pihole.h` (watch)
  and at the top of `src/pkjs/index.js` (phone). New AppMessage keys also go into
  `messageKeys` in `package.json`.
- **Phone code is ES5** – PebbleKit JS runs in JavaScriptCore on iOS.
- **Texts** live in `src/c/i18n.c` (watch), `src/pkjs/index.js` and `src/pkjs/config.js` (phone),
  always in German and English.
- **Pi-hole API:** only Pi-hole 6 (`/api/...`). Reuse the session – Pi-hole has a limited
  number of API sessions.

AI-assisted contributions are welcome if you have tested them yourself and mention it in the
pull request.
