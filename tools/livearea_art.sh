#!/bin/sh
# The LiveArea images (port/vita/sce_sys) from the owner's art
# (port/vita/art/livearea-art.jpg, 1080x1920, made by the project's owner):
# the background (from 330 pixels down), the Start gate the part of it the
# gate covers (the Vita's gate sits at 280,62 of the 840x500 background: over
# the Spartan's helmet and gun), and
# pic0 the Spartan and the scene, the icon the Spartan's helmet; the owner's
# HALO CE VITA logo (port/vita/art/logo.png) top left, above the gate on the
# background, and on pic0. The Vita
# wants 8-bit palette PNGs. Needs ImageMagick 7 (magick).
#
#     sh tools/livearea_art.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
art=$root/port/vita/art/livearea-art.jpg
logo=$root/port/vita/art/logo.png
out=$root/port/vita/sce_sys
magick "$art" -crop 1080x643+0+330 +repage -resize 840x500 \( "$logo" -resize 300x \) -gravity northwest -geometry +14+6 \
	-composite -strip -colors 256 PNG8:"$out/livearea/contents/bg.png"
magick "$art" -crop 360x203+360+410 +repage -resize '280x158!' -strip -colors 256 PNG8:"$out/livearea/contents/startup.png"
magick "$art" -crop 1080x613+0+330 +repage -resize '960x544!' \( "$logo" -resize 420x \) -gravity northwest -geometry +20+14 \
	-composite -strip -colors 256 PNG8:"$out/pic0.png"
magick "$art" -crop 210x210+380+395 +repage -resize 128x128 -strip -colors 256 PNG8:"$out/icon0.png"
