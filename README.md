# Cyklopočítač – ESP32-C3 firmware + React Native aplikace

Chytrý cyklocomputer: **ESP32-C3-Zero** s TFT displejem a GPS jako
samostatné zařízení, a doprovodná **Android aplikace** (React Native),
propojené přes **Bluetooth Low Energy**. Zdrojem pravdy pro jízdu
(trip, čas, max, stopa) je vždy počítač na kole – appka se synchronizuje
podle jeho telemetrie oběma směry.

```
firmware/CykloComp/CykloComp.ino   – Arduino sketch pro ESP32-C3
app/                                – React Native (Android) zdrojový kód
```

## Hardware (dáno, neměnit)

| Komponenta | Piny / poznámka |
|---|---|
| Waveshare ESP32-C3-Zero | jen BLE, 4 MB flash, ~400 KB RAM, bez PSRAM |
| ST7789 2.0" TFT 240×320, na výšku | CS=3, DC=2, RST=1, MOSI=6, SCLK=4 |
| GPS (TinyGPSPlus, UART) | RX=20, TX=21, 9600 baud |
| 3 tlačítka (pull-up, aktivní LOW) | BTN1=GPIO8, BTN2/BTN3 configurovatelné `#define` |
| Li-Po + TP4056 | bez pinu podsvícení |

## Firmware – knihovny (Arduino IDE)

`Adafruit_GFX`, `Adafruit_ST7789`, `TinyGPSPlus`, `NimBLE-Arduino` (v2.x),
`Preferences` (NVS, součást ESP32 core). Nainstaluj přes Library Manager,
otevři `firmware/CykloComp/CykloComp.ino` v Arduino IDE, vyber desku
"ESP32C3 Dev Module", nahraj.

**Ve složce sketche smí být jen tento jeden `.ino` soubor** – to je
podmínka Arduino IDE, aby překlad prošel.

## Testování přes USB (bez telefonu/appky)

Než je hotová appka (nebo kdykoliv chceš rychle ladit displej/protokol
bez BLE), firmware zvládá **stejný textový protokol i po USB** –
nativní USB-CDC, žádný adaptér navíc.

1. V Arduino IDE: **Tools → USB CDC On Boot → Enabled** (jinak `Serial`
   poběží jen po klasickém UART-USB mostu, ne po nativním USB portu).
2. Nahraj firmware, otevři Serial Monitor (115200 baud) nebo spusť
   `python3 tools/usb_console.py [PORT]` (potřeba `pip install pyserial`).
3. Piš přímo protokolové příkazy (`RIDE:START`, `SCREEN:2`,
   `CFG:FIELDS:0,1,3,4`, `SET_TZ:2`, …) a sleduj JSON telemetrii, kterou
   ESP vypisuje 1×/s. `tools/usb_console.py` navíc umí `testroute` –
   pošle ukázkovou trasu přes `RT:BEGIN/RT:P/RT:END` na otestování mapy.

BLE a USB kanál běží současně a sdílejí stejnou logiku (`handleCommand`),
takže appka a USB konzole se nijak nebijí – je to čistě přídavný ladicí
kanál pro tuhle fázi vývoje.

## BLE protokol (musí sedět firmware ↔ appka)

- Service UUID: `a5c40001-2f0b-4f6e-9d3a-8c1e2b7d9f10`
- Telemetrie (`a5c40002-…`, READ+NOTIFY, 1×/s):
  `{"fix":0/1,"spd":kmh,"lat":…,"lng":…,"alt":m,"dst":km,"tim":s,"sat":n,"rid":0/1,"pau":0/1}`
- Příkazy (`a5c40003-…`, WRITE + WRITE_NR):
  `RIDE:START|STOP|PAUSE|RESUME`, `RESET_TRIP`, `SET_TZ:<h>`, `SCREEN:<n>`,
  `CFG:FIELDS:a,b,c,d`, trasa `RT:BEGIN`/`RT:P:lat,lng;…`/`RT:END`/`RT:CLEAR`,
  ulice `MP:BEGIN`/`MP:P:…`/`MP:END` (segmenty odděleny `999,999`)

## Mobilní aplikace

Zdrojový kód v `app/src`. Podrobný postup scaffoldingu (RN CLI init,
Android nastavení, oprávnění, Google Maps klíč) je v `app/README.md`.

Klíčové vrstvy:
- `ble/BleManager.js` – singleton, MTU 185, reconnect po reloadu appky,
  chunkované odesílání trasy/ulic s pacingem
- `services/routing.js`, `services/overpass.js` – BRouter/OSRM a Overpass
  s fallback servery
- `gamification/gamification.js` – body/level/streak/odznaky, čistá logika
- `screens/*` – Jízda / Mapa / Historie / Profil

## Akceptační test (ruční, na reálném HW)

1. **Připojení** – appka najde `CykloComp`, telemetrie teče do 2 s;
   po reloadu appky (bez vypnutí ESP) se spojení převezme bez skenování
   (`BleManager.adoptExistingConnection`).
2. **Start z appky** – `RIDE:START` → obě zařízení od 00:00, na displeji
   se rozsvítí červená REC tečka.
3. **Pauza** – `RIDE:PAUSE` z appky → na ESP žluté `‖`, čas stojí;
   zrušení BTN3 na ESP → v appce zmizí PAUZA badge (telemetrie `pau:0`).
4. **Start/stop z ESP** – BTN2 na ESP → appka detekuje `rid:1` bez toho,
   aby sama poslala `RIDE:START` (`zeZarizeni` větev v `RideScreen`), a po
   `rid:0` jízdu uloží i s body.
5. **Restart appky během jízdy** – appka po startu vidí `rid:1` už při
   prvním telemetrickém paketu → jízda pokračuje, počítadla na ESP se
   nevynulují.
6. **Trasa** – naklikání bodů v Mapa (plánovací režim) → modrá čára po
   silnicích (BRouter/OSRM) → „→ Do ESP" → na displeji trasa + šedé ulice
   + „DO CILE: X.X km"; při stání se mapa nepřekresluje (gating 40 m /
   rychlost / 3 s).
7. **Rychlost při stání** – EMA + práh 2,5 km/h → `0.0`, ne GPS šum.
8. **⚙ konfigurace polí** – přeskládání 2×2 mřížky přežije restart ESP
   (uloženo v NVS) i restart appky (AsyncStorage).

## Oprava "rozbitého" textu na displeji

Anti-flicker vykreslování (`drawField`) původně mazalo jen pevně
definovaný obdélník pole (`x,y,w,h`). Pokud byl skutečný vykreslený text
širší/vyšší než tento odhad (delší číslo, jiná šířka znaku), zbytky
starých znaků zůstávaly na displeji a vypadalo to jako "rozbitý" text.
Teď se plocha na smazání počítá přes `tft.getTextBounds()` – sjednocení
bounding boxu starého i nového textu – takže se vždy smaže přesně to, co
bylo předtím vykreslené. Zároveň je zapnuté `tft.setTextWrap(false)`,
aby se delší text nezalomil přes sousední panel.

## Hranice systému

- Skutečné rastrové mapové dlaždice na ESP32-C3 **nejdou** (jen 4 MB
  flash) – firmware kreslí vektorové ulice/trasu/stopu. Upgrade cesta:
  ESP32-S3 + PSRAM + SD karta pro rastrové dlaždice.
- Overpass/BRouter/OSRM jsou veřejné služby třetích stran – appka se
  identifikuje vlastním User-Agentem, nezatěžuje je agresivně (debounce
  700 ms) a má fallback servery, ale výpadek na jejich straně (rate-limit,
  údržba) může selhat – řeší se `Alert` s chybovou hláškou, ne tichým pádem.
