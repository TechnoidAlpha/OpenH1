#!/bin/sh
# Renders a share picture of this folder (1200 x 630, from its HTML) into
# public/media, with headless Chrome at twice the size, then scaled down
# (rendered taller and cropped: headless Chrome's page is shorter than its
# window):
#   og/render.sh maps    # og/maps.html -> public/media/og-maps.jpg
#   og/render.sh home    # og/home.html -> public/media/og.jpg
set -eu
here=$(cd "$(dirname "$0")" && pwd)
name=$1
out=og-$name.jpg
[ "$name" = home ] && out=og.jpg
chrome=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$chrome" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=2 \
	--window-size=1200,800 --screenshot="$work/og.png" "file://$here/$name.html" >/dev/null 2>&1
python3 -c 'import sys; from PIL import Image
Image.open(sys.argv[1]).convert("RGB").crop((0, 0, 2400, 1260)).resize((1200, 630), Image.LANCZOS).save(sys.argv[2], quality=88)' \
	"$work/og.png" "$here/../public/media/$out"
echo "public/media/$out"
