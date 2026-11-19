#!/usr/bin/env bash
# Regenerates every PNG in resources/images and appstore/ from the SVG sources
# and checks the UI icons. Needs rsvg-convert, magick (ImageMagick 7) and oxipng:
#   nix shell nixpkgs#librsvg nixpkgs#imagemagick nixpkgs#oxipng -c bash tools/icons.sh
set -euo pipefail
cd "$(dirname "$0")/.."

# UI icons are alpha masks: black pixels with alpha in {0, 85, 170, 255}, which
# the SDK stores as a 2-bit palette. Coverage below T1 is dropped; the two
# middle levels carry the antialiasing.
T1=0.12 T2=0.42 T3=0.78

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# quantise_alpha RAW OUT: the alpha channel of RAW cut to four levels, as a grey image.
quantise_alpha() {
  magick "$1" -alpha extract \
    -fx "u<$T1?0:(u<$T2?1/3:(u<$T3?2/3:1))" -depth 8 "$2"
}

# apply_alpha COLOUR ALPHA OUT: COLOUR with ALPHA as its alpha channel.
apply_alpha() {
  magick "$1" -alpha off "$2" -alpha off -compose CopyOpacity -composite \
    -strip -depth 8 "$3"
}

render_icon() {
  local svg=$1 out=$2 size
  rsvg-convert "$svg" -o "$tmp/raw.png"
  size=$(magick identify -format '%wx%h' "$tmp/raw.png")
  quantise_alpha "$tmp/raw.png" "$tmp/alpha.png"
  magick -size "$size" xc:black "$tmp/black.png"
  apply_alpha "$tmp/black.png" "$tmp/alpha.png" "$out"
}

# The launcher icon keeps its greys (a pot with a white halo) on the same four levels.
render_menu_icon() {
  rsvg-convert resources/icons/menu_icon.svg -o "$tmp/raw.png"
  quantise_alpha "$tmp/raw.png" "$tmp/alpha.png"
  magick "$tmp/raw.png" -alpha off -colorspace Gray -posterize 4 "$tmp/grey.png"
  apply_alpha "$tmp/grey.png" "$tmp/alpha.png" resources/images/menu_icon.png
}

for svg in resources/icons/*.svg; do
  name=$(basename "$svg" .svg)
  case $name in
    menu_icon) render_menu_icon ;;
    *) render_icon "$svg" "resources/images/$name.png" ;;
  esac
done

rsvg-convert -w 80 -h 80 appstore/icon.svg -o appstore/icon-small.png
rsvg-convert -w 144 -h 144 appstore/icon.svg -o appstore/icon-large.png

oxipng -o max --strip safe -q resources/images/*.png appstore/*.png

# Every UI icon must be black with alpha in {0, 85, 170, 255} and match its SVG size.
status=0
printf '%-22s %-7s %5s %5s %5s %5s\n' icon size a0 a85 a170 a255
for png in resources/images/icon_*.png; do
  name=$(basename "$png" .png)
  want=$(sed -n 's/.*<svg [^>]*width="\([0-9]*\)" height="\([0-9]*\)".*/\1x\2/p' "resources/icons/$name.svg")
  got=$(magick identify -format '%wx%h' "$png")
  magick "$png" -depth 8 txt:- | grep -o '#[0-9A-Fa-f]\{8\}' |
    awk -v name="$name" -v want="$want" -v got="$got" '
      {
        rgb = substr($0, 2, 6); a = substr($0, 8, 2)
        if (rgb != "000000" && !seen[rgb]++) bad = bad " non-black(#" rgb ")"
        if (a == "00") n0++; else if (a == "55") n85++; else if (a == "AA") n170++; else if (a == "FF") n255++
        else if (!seen[a]++) bad = bad " alpha=#" a
      }
      END {
        printf "%-22s %-7s %5d %5d %5d %5d\n", name, got, n0, n85, n170, n255
        if (got != want) bad = bad " size(svg says " want ")"
        if (bad != "") { print "  FAIL:" bad; exit 1 }
      }' || status=1
done
exit $status
