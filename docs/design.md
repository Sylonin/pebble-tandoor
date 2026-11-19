# Design

Tandoor for Pebble has three parts:

- The watch app (`src/c/`) shows menus, the recipe, the cooking steps and the timers. It keeps the timers and the cooking session in persistent storage, so they survive the app closing.
- PebbleKit JS on the phone (`src/pkjs/`) talks to the Tandoor REST API and turns its JSON into small AppMessages. It keeps the server address, the API token and the settings in `localStorage`.
- The settings page (`src/config.html`) asks for the server address and the API token, tests them and saves them through PKJS.

The watch never talks to the network. PKJS never decides what the watch shows; it answers requests.

## Screens

| Screen | File | What it shows | Buttons |
| --- | --- | --- | --- |
| Home | `home_win.c` | A "Cooking" section with the session to resume and the running timers, then the ways to browse: Meal plan, Search, Recent, Books, Keywords, All recipes, Surprise me. A setup card when PKJS reports no server. | Select opens a row. |
| List | `list_win.c` | One list of recipes, books, keywords or meal plan entries, loaded 20 at a time. The next page loads when the last row is reached. | Select opens the row: a recipe, a book, a keyword folder or a keyword. |
| Recipe | `recipe_win.c` | A header with the name, times and servings, a Start cooking row, a servings row, the ingredients (a checklist scaled to the servings) and the step list. | Select on Start starts at step 1; on Servings opens the servings stepper; on an ingredient ticks it; on a step starts cooking at that step. |
| Cook | `cook_win.c` | One step: progress bar, step number and name, the instruction in large type, the step's ingredients and a timer card. | Up and Down scroll; past the top or bottom they move to the previous or next step. Select starts, pauses or resumes the step's timer, or moves on when the step has none. Long Select opens the step menu. |
| Done | `cook_win.c` | The last page after the last step. | Select logs the cook in Tandoor (once), Back returns. |
| Timers | `timers_win.c` | Every timer with a live countdown. | Select opens the timer menu: pause or resume, add a minute, go to the step, cancel. |
| Alert | `alert_win.c` | A finished timer: full screen, vibration and a chime on the speaker until dismissed. | Up adds a minute, Select dismisses, Down opens the step. |

The status bar on every screen but the alert shows the time, and the next timer as an orange pill.

## Look

Tandoor's brand is charcoal with an orange and amber flame. The app uses a black background with white text, amber (`GColorChromeYellow`) for highlights and selection, orange (`GColorOrange`) for timers and alerts, and light grey for secondary text. Icons are 2-bit palette PNGs drawn as alpha masks; `ui_icon_draw` repaints their palette in the colour a row needs, so one file serves highlighted and plain rows.

Fonts: Gothic 24 Bold for titles, Gothic 18 Bold and Gothic 14 for menu rows, Gothic 24 for step text (it is read at arm's length), and LECO numbers for timers.

## Timers

- `timers.c` keeps up to `MAX_TIMERS` (6) timers. A timer has a recipe id, a step index, a label, its duration, and either an end time (running) or the seconds left (paused). Each timer is one persist key.
- While the app runs, a second tick updates the status bar and fires timers that reached their end.
- When the app closes, `timers.c` schedules one Wakeup for the earliest running timer. If the system refuses that time (`E_RANGE`: another Wakeup within a minute), it tries up to a minute earlier, then later; an early launch shows the timer still counting and alerts on time. A Wakeup launch opens the alert, and the app schedules the next Wakeup when it closes again.
- The alert repeats vibration and a chime every few seconds for up to two minutes. The speaker obeys the watch's mute and Quiet Time (`speaker_is_muted`).
- The launcher glance shows the next running timer as a countdown.

## Cooking session

`model.c` stores the recipe id, the name, the step and the servings of the last recipe cooked. Home offers to resume it; the recipe is fetched again, since a recipe doesn't fit in persistent storage. The session ends on the Done page or after 12 hours.

## Messages

Every message has `Cmd`. Requests from the watch carry `Req`, a number that grows with each request; PKJS copies it into every answer. The watch drops answers whose `Req` is not the one it waits for, so a list the user left doesn't fill the next one.

PKJS sends one AppMessage at a time (a second one in flight is rejected). The watch holds its requests until PKJS sends `READY`, or for 6 seconds.

### Watch to phone

| Cmd | Name | Keys | Answer |
| --- | --- | --- | --- |
| 1 | HELLO | | `STATUS`, then `SETTINGS` |
| 2 | LIST | `Req`, `Kind`, `Id`, `Page` (from 1), `Query` (search only) | `ITEMS` chunks, or `ERROR` |
| 3 | RECIPE | `Req`, `Id` (0: a random recipe) | `RECIPE`, `INGREDIENTS` chunks, one `STEP` per step, `DONE`; or `ERROR` |
| 4 | COOKED | `Req`, `Id`, `Servings` (×100) | `RESULT`. PKJS posts `{recipe, servings}` to `/api/cook-log/`, leaving out `servings` when it is 0. |

### Phone to watch

| Cmd | Name | Keys |
| --- | --- | --- |
| 10 | READY | |
| 11 | STATUS | `State` (0 ready, 1 not set up), `Text` (the server's host name) |
| 12 | SETTINGS | `Backlight` (0 or 1), `Sound` (0 or 1) |
| 13 | ITEMS | `Req`, `Index` (position of the chunk's first record in the whole list), `Data` (records), `Done` (1 on the page's last chunk), `More` (1 when another page exists and the list holds fewer than 200 records), `Total` (Tandoor's count of the whole list, or -1) |
| 14 | RECIPE | `Req`, `Id`, `Name`, `Desc`, `Servings` (×100), `ServText`, `Time1` (working minutes), `Time2` (waiting minutes), `Rating` (0 to 5), `Count` (steps), `Total` (ingredients), `Sub` (keywords, comma separated) |
| 15 | INGREDIENTS | `Req`, `Index` (position of the chunk's first ingredient), `Data` (records) |
| 16 | STEP | `Req`, `Index`, `Name`, `Text`, `Time1` (timer seconds, 0 for none), `Flags` (bit 0: the time was read from the text) |
| 17 | DONE | `Req` |
| 18 | ERROR | `Req`, `State` (error code), `Text` (one sentence for the user) |
| 19 | RESULT | `Req`, `State` (0 or an error code), `Text` |

Error codes: 1 not set up, 2 server unreachable, 3 token refused (401 or 403), 4 not found, 5 server error or unreadable answer, 6 voice search failed (watch only). A 404 on a list page after the first means the list ended: PKJS answers an empty last page.

### List kinds

| Kind | Name | `Id` | Tandoor request |
| --- | --- | --- | --- |
| 1 | PLAN | | `GET /api/meal-plan/?from_date=<today>&to_date=<today+6>` (phone's local date) |
| 2 | RECENT | | `GET /api/recipe/?sort_order=-lastviewed` |
| 3 | ALL | | `GET /api/recipe/?sort_order=name` |
| 4 | BOOKS | | `GET /api/recipe-book/` |
| 5 | BOOK | book id | `GET /api/recipe/?books=<id>`, or `?filter=<filter id>` for a book defined by a saved filter |
| 6 | KEYWORDS | parent id, 0 for the top level | `GET /api/keyword/?root=<id>` |
| 7 | KEYWORD | keyword id | `GET /api/recipe/?keywords=<id>` |
| 8 | SEARCH | | `GET /api/recipe/?query=<Query>` |

Lists use `page_size=20`, except the meal plan: Tandoor orders it by id, so PKJS fetches the whole week (`page_size=200`), sorts it by day, meal type and time, keeps it for 10 minutes, and pages it by 20 itself. For `KEYWORDS` with a parent, the first record of page 1 is "All <parent>", a keyword record with the parent's id, and `Total` counts it.

### Records

`Data` holds records joined without separators. Each record ends with `\x1e`; its fields are separated by `\x1f`. Strings never contain control characters.

List record: `type`, `id`, `title`, `subtitle`.

| type | Meaning | Select opens |
| --- | --- | --- |
| `r` | recipe | Recipe |
| `b` | book | List, kind BOOK |
| `f` | keyword with children | List, kind KEYWORDS |
| `k` | keyword without children | List, kind KEYWORD |
| `n` | meal plan entry without a recipe | nothing |

Recipe subtitles: total time and servings (`25 min · 4 servings`). Meal plan subtitles: day and meal type (`Today · Dinner`, `Tomorrow · Lunch`, `Fri 21 · Dinner`). Book and keyword records have an empty subtitle.

Ingredient record: `step`, `flags`, `amount`, `unit`, `unit plural`, `food`, `food plural`, `note`.

- `step`: index of the step that uses it, empty when none.
- `flags`: bit 0 header (only `food` holds its text: Tandoor's `note`, else the food's name), bit 1 no amount.
- `amount`: the amount ×1000, rounded. The watch scales it to the chosen servings and formats it.
- Plural fields are empty when Tandoor has none.

### Limits

PKJS cuts every string to these sizes in bytes on a UTF-8 character boundary, so the watch's buffers (in `model.h`, which hold one more byte for the terminator) always fit.

| Field | Bytes |
| --- | --- |
| list title / subtitle | 63 / 47 |
| recipe name / description / servings text / keywords | 63 / 255 / 23 / 63 |
| step name / step text | 47 / 1799 |
| unit / food / note | 23 / 47 / 47 |
| records per list | 200 |
| steps / ingredients per recipe | 40 / 80 |
| `Data` per message | 1400 |
| ERROR and RESULT text / STATUS host | 95 / 47 |

The watch's inbox is 2400 bytes.

### Steps

PKJS flattens the steps: a step that includes another recipe (`step_recipe_data`) is replaced by that recipe's steps, named `<recipe> · <step name>`. Step text comes from `instructions_markdown` (Tandoor's rendered HTML, with ingredient templates filled in), converted to plain text: paragraphs become blank lines, list items become `• ` lines, entities are decoded.

A step's timer is Tandoor's `time` (minutes) when set. Otherwise PKJS reads the first duration in the text and sets flag bit 0: `5 minutes`, `1-2 hours` (the lower end of a range), `30 sec`, `half an hour`, `1 hour 30 minutes`. The watch caps a timer at 24 hours.

## Storage

### Watch (persist keys)

| Key | Holds |
| --- | --- |
| 1 | `Settings` and the storage version |
| 2 | `Session` (last recipe cooked) |
| 10 to 15 | one `Timer` each |

Bump `STORAGE_VERSION` in `model.c` when a stored struct changes; older data is then dropped.

### Phone (`localStorage`)

| Key | Holds |
| --- | --- |
| `server` | Base URL without a trailing slash |
| `token` | API token |
| `settings` | JSON `{backlight, sound}` |

### Settings page requests (`Pebble.sendMessage('pkjs', …)`)

| type | Fields | Answer |
| --- | --- | --- |
| `status` | | `{ok, server, hasToken, settings}` |
| `save` | `server`, `token`, `settings` | Tests the server with `GET /api/recipe/?page_size=1`, then saves. An empty token keeps the saved one when the host is unchanged; a pasted `Bearer ` prefix is dropped. `{ok, message, count, server}`, where `server` is the normalised address. |
| `signout` | | Forgets the token. `{ok}` |

After `save` and `signout`, PKJS sends `STATUS` and `SETTINGS` to the watch.
