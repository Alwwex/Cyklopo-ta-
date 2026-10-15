# Náhled displeje na PC

Přeloží **stejný** `../CykloComp/CykloComp.ino` pro Linux/macOS/WSL: displej je
jen pole pixelů (`Adafruit_GFX::GFXcanvas16`), Bluetooth, NVS a flash jsou
nahrazené jednoduchými náhradami ve `shim/`. `preview.cpp` odsimuluje 14 min
jízdy po trase s ulicemi a uloží obrázky všech obrazovek ve všech tématech
do `out/` (+ `out/prehled.png`).

```bash
pip install pillow
ARDUINO_LIBS=~/Arduino/libraries ./build_preview.sh   # potřebuje Adafruit GFX + TinyGPSPlus
```

Hodí se při úpravách vzhledu – změna → pár sekund → obrázek, bez nahrávání do ESP.
