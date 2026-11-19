# Tandoor for Pebble

A Pebble Time 2 app for [Tandoor](https://tandoor.dev), the self-hosted recipe manager. It browses recipes (meal plan, voice search, recent, books, keywords, all), shows a recipe with a servings stepper and an ingredient checklist, and cooks it step by step with timers that ring on the watch even after the app closes. `README.md` describes what users see. `docs/design.md` describes the screens, the look, the timers, the message protocol and storage. When a change alters something `docs/design.md` describes, update it in the same commit.

It targets emery (Pebble Time 2) only: it uses the speaker, the 200×228 layout and touch navigation, and the C code has no `PBL_IF_*` branches for other displays.

## Layout

| Path | Runs in | Job |
| --- | --- | --- |
| `src/c/` | watch | Windows: `home_win.c`, `list_win.c`, `recipe_win.c` (with the servings stepper), `cook_win.c` (steps and the Done page), `timers_win.c`, `alert_win.c`, `msg_win.c`. State: `model.c` (settings, session, the loaded recipe), `timers.c` (timers, ticks, Wakeup). Other: `comm.c` (AppMessage), `alert.c` (vibration and speaker), `glance.c`, `ui.c` (colours, fonts, icons, formatting, status bar). |
| `src/pkjs/index.js` | PebbleKit JS on the phone | XHR to Tandoor, the AppMessage queue, the settings page's requests, `localStorage`. |
| `src/pkjs/tandoor.js` | PebbleKit JS and Node | Pure functions: URLs, records, HTML to text, durations, truncation, chunking, errors. |
| `src/config.html` | bundled settings page | One file with inline script. Talks to PKJS through `Pebble.sendMessage('pkjs', …)`. |
| `test/` | Node | Tests of `tandoor.js`. |
| `tools/mock-tandoor/` | Python | A fake Tandoor server with fixture recipes, for the emulator. |
| `docs/screenshots/` | | README screenshots (`pebble screenshot --scale 2`) taken in the emulator against the mock server, never a real account. Retake the affected ones after a visible UI change; `oxipng -o max --strip safe` them. |
| `resources/icons/` | | SVG sources of the icons. `icons` (devenv script, runs `tools/icons.sh`) renders them to `resources/images/` and `appstore/`. |
| `devenv.nix`, `devenv.yaml`, `.envrc` | | Dev shell: Node.js, pebble-tool, the emulator's libraries, linters, git hooks, the icon renderer. |
| `.clang-tidy`, `biome.json` | | Linter settings for the C and the JS, HTML and CSS. |

## Commands

`devenv.nix` provides Node.js, pebble-tool 5.1.2 in a Python 3.12 venv, and the shared libraries that the emulator (qemu-pebble) loads through nix-ld. `.envrc` loads it with direnv, so run commands directly in this directory. pebble-tool keeps the SDK (4.44) in `~/.local/share/pebble-sdk`; on a new machine run `pebble sdk install latest` once.

The shell has no host C compiler and unsets `CC` and `AR`, because waf would use them instead of the SDK's ARM compiler (`gcc: error: unrecognized command-line option '-mthumb'`). Don't add a C compiler to `devenv.nix`.

`git commit` runs these hooks through prek: ripsecrets, treefmt (nixfmt), Biome on staged JS, JSON and HTML files, and clang-tidy when a file in `src/c` changes. Run them on every file with `prek run --all-files`.

```sh
pebble build                                  # emery; the build prints the free heap
npm test                                      # node --test test/*.test.cjs
lint                                          # clang-tidy on src/c, then Biome
icons                                         # re-render the PNG icons from resources/icons/*.svg
python3 tools/mock-tandoor/server.py          # fake Tandoor on :8765, token mock-token
pebble install --emulator emery --vnc
pebble screenshot --emulator emery --vnc --no-open --scale 2 /tmp/shot.png
pebble emu-button --emulator emery --vnc click select   # back, up, select, down
pebble emu-app-config --emulator emery --vnc --uuid 261a265f-72a5-40a7-911d-1809026156c0
pebble kill && pebble wipe                    # stop a stuck emulator and clear its storage
```

Setup problems seen on this machine:

- The first install after the emulator starts often fails with `Connection refused`. Retry every 10 seconds until it prints `App install succeeded`.
- `pebble logs --emulator emery` prints nothing through a pipe. Run it under a pty: `script -q -f -c "pebble logs --emulator emery" /tmp/logs.txt`.
- After changing `messageKeys`, run `pebble clean` before building. waf doesn't regenerate `message_keys.auto.h`.
- `emu-app-config` opens the page in a browser. Without one, open `http://localhost:<port>/config/<uuid>/`, where the port is the emery `pypkjs` port in `/tmp/pb-emulator.json`. After reinstalling the app, reload the page: its PKJS connection goes stale.
- Opening Search in the emulator shows the firmware's dictation screen; the emulator has no microphone, so voice search ends in an error there.

## Verifying a change

- PKJS logic: run `npm test`, and add or change a test in `test/tandoor.test.cjs` for new behaviour.
- Watch app: start `tools/mock-tandoor/server.py`, point the settings page at `http://127.0.0.1:8765` with token `mock-token`, then build, install, take screenshots and press through the changed screens. `--delay MS` on the mock slows every answer, to see loading states.
- Timers: a step whose text says "5 seconds" gets a 5-second timer, which is the quickest way to see the alert. To test the Wakeup, start a timer, press Back until the app closes, and wait.
- Watch the heap line `pebble logs` prints when the app closes: `Still allocated <0B>` means every window and layer was freed.
- The emulator plays neither the speaker nor the vibration motor, has no microphone and no touchscreen. Sound, vibration, voice search and touch navigation need a Pebble Time 2. Report them as unverified.

## Rules that are easy to break

### Watch and phone contract

- `Cmd` numbers, list kinds, error codes and record fields are defined in three places: `src/c/comm.h` and `comm.c`, `src/pkjs/tandoor.js` and `index.js`, and the Messages section of `docs/design.md`. Change all three together.
- String limits are in `model.h` (buffer sizes, one byte more than PKJS sends) and in `tandoor.js` (cut sizes). PKJS cuts every string on a UTF-8 character boundary.
- Records use `\x1f` between fields and `\x1e` after each record. PKJS removes control characters from fields, so these never appear inside text.
- The watch holds its messages until PKJS sends `READY`, or for 6 seconds. PKJS sends one AppMessage at a time, because a second one in flight is rejected.
- Every request carries `Req`; answers echo it. The watch drops answers it no longer waits for. A request without an answer for 25 seconds shows "Your phone didn't answer".

### Timers

- `timers.c` owns persist keys 10 to 15; `model.c` owns 1 and 2. `KEY_LAST` in `model.c` must cover every key, because a storage version change deletes keys 2 to `KEY_LAST`.
- While the app runs, the tick fires timers and no Wakeup is scheduled (`timers_init` cancels them). `timers_deinit` schedules one Wakeup for the earliest running timer, trying earlier times first when the system refuses one.
- Ringing timers stay ringing until dismissed, snoozed or cancelled; a timer left ringing for 10 minutes is dropped at the next launch.

### Windows

- One recipe is loaded at a time (`model_recipe()`). `recipe_win_push` closes the cooking window, the servings stepper and the old recipe window before loading another recipe.
- Windows that need a second tick register with `timers_observe` in `.appear` and unregister in `.disappear`, so a covered window doesn't redraw.
- `ui_icon_draw` repaints an icon's palette on every call; never hand an icon bitmap to a layer that draws it later (ActionBarLayer, BitmapLayer).

## Conventions

- C: `#pragma once`; static functions start with `prv_`; public functions start with their module name. `-Werror` is on: keep `printf` format strings literal.
- PKJS: `var` and function declarations, no arrow functions. `pebble build` bundles `src/pkjs` with the SDK's webpack 1.15, whose parser accepts ES2015 only: no `async`/`await`, no `**` and no `catch {}` without a binding.
- Tests: CommonJS `*.test.cjs` files using `node:test`.
- Commits: Conventional Commits, with a scope where one fits (`fix(watch): …`).

## Documentation

- Pebble SDK: https://developer.repebble.com/llms.txt indexes every page. Add `.md` to a page URL to get Markdown. Speaker, touch and dictation: the emery SDK's `pebble.h`.
- Tandoor API: `<server>/openapi/` on any instance; the source is github.com/TandoorRecipes/recipes (`cookbook/serializer.py`, `cookbook/views/api.py`).
- Pebble app behaviour (permissions, the settings page, PKJS engines): github.com/coredevices/mobileapp, `libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/{js,permissions}/`.
