#!/bin/bash
# Nahled displeje na PC (Linux / macOS / WSL), bez ESP32 a bez displeje.
# Potrebuje g++ (C++17), Python 3 + Pillow (pip install pillow) a Arduino knihovny
# "Adafruit GFX Library" a "TinyGPSPlus" (stejne jako pro Arduino IDE).
#
#   ARDUINO_LIBS=~/Arduino/libraries ./build_preview.sh
#
# Vysledne obrazky: firmware/host_preview/out/*.png
set -e
cd "$(dirname "$0")"
LIBS="${ARDUINO_LIBS:-$HOME/Arduino/libraries}"
GFX=$(ls -d "$LIBS"/Adafruit*GFX* | head -1)
GPS=$(ls -d "$LIBS"/TinyGPS* | head -1)
[ -d "$GFX" ] || { echo "Nenalezena Adafruit GFX v $LIBS"; exit 1; }
[ -d "$GPS" ] || { echo "Nenalezena TinyGPSPlus v $LIBS"; exit 1; }

g++ -std=c++17 -O1 -w -DARDUINO=10819 -DBOARD_PROFILE=1 \
  -Ishim -I"$GFX" -I"$GPS/src" \
  preview.cpp "$GFX/Adafruit_GFX.cpp" "$GPS/src/TinyGPS++.cpp" \
  -o preview_bin
./preview_bin | grep -v '^{' | grep -v '^STATUS:' || true

python3 - <<'EOF'
import glob, os
from PIL import Image
for p in sorted(glob.glob("out/*.ppm")):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
    os.remove(p)
# jeden prehledovy obrazek se vsemi obrazovkami
files = sorted(f for f in glob.glob("out/[1-3][0-3]_*.png"))
if files:
    ims = [Image.open(f) for f in files]
    w, h = ims[0].size
    cols = 4
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 20) + 20, rows * (h + 20) + 20), (40, 40, 40))
    for i, im in enumerate(ims):
        sheet.paste(im, (20 + (i % cols) * (w + 20), 20 + (i // cols) * (h + 20)))
    sheet.save("out/prehled.png")
print("hotovo -> out/")
EOF
