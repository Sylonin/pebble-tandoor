# Icon sources

Every `*.svg` here is rendered by `tools/icons.sh` into `resources/images/<name>.png`. Run it after editing a source:

```sh
nix shell nixpkgs#librsvg nixpkgs#imagemagick nixpkgs#oxipng -c bash tools/icons.sh
```

The script also renders `appstore/icon.svg` into `appstore/icon-small.png` (80x80) and `appstore/icon-large.png` (144x144), and checks every `icon_*.png`.

## Format

- The `width` and `height` of each SVG are the size of the PNG in pixels.
- UI icons (`icon_*.svg`) are alpha masks. The script makes every pixel black and cuts the alpha to 0, 85, 170 or 255, so the SDK stores them as `2BitPalette`. The watch repaints them in the colour a row needs.
- 12 to 20 px icons are drawn on the pixel grid: 2 px strokes centred on whole pixels, so straight edges stay crisp. The 48 px icons use 3 px strokes shifted by half a pixel for the same reason.
- `menu_icon.svg` is the launcher icon (25x25). It keeps greys, on the same four alpha levels, and has a white halo so it reads on a dark highlight.

## Sources and licences

- Lucide (https://lucide.dev; ISC licence, Copyright (c) Lucide Icons and Contributors; Feather-derived icons MIT, Copyright (c) Cole Bemis): `icon_recent` and `icon_book` (history and book-open), `icon_tag`, `icon_search` (mic), `icon_resume` (chef-hat, redrawn), `icon_timer`, `icon_plan` (calendar), `icon_clock`, `icon_users`, `icon_plus`, `icon_dismiss` (check), `icon_step` (arrow-right), `icon_chevron` (chevron-right), `icon_bell` (bell-ring), `icon_done` (circle-check), `icon_setup` (smartphone and settings), `icon_offline` (cloud-off), `icon_empty` (search-x), `icon_random` (dices, reduced to one die), `icon_all` (list), `icon_check_on` and `icon_check_off` (circle-check and circle). Most were redrawn on the target pixel grid, with changed proportions, so they no longer match Lucide's geometry exactly. The 48 px icons keep Lucide's paths unchanged.
- `icon_play`, `icon_play_small` and `icon_pause`, `menu_icon.svg` and `appstore/icon.svg` are drawn for this project and fall under the project's licence (GPL-3.0-or-later).

`LICENSE-lucide` holds Lucide's licence as published: the ISC licence for Lucide, and the MIT licence for the icons Lucide took from Feather (among those used here: calendar, check, chevron-right, circle, clock, arrow-right). Keep it next to the derived icons.
