# Co doobjednat

Ceny jsou orientační (podzim 2026), obchody: [TME](https://www.tme.eu/cz/)
(rychle do ČR, profi součástky), [GM Electronic](https://www.gme.cz),
[LaskaKit](https://www.laskakit.cz), [Botland](https://botland.cz) (Adafruit),
AliExpress (levné, 2–4 týdny), [JLCPCB](https://jlcpcb.com) (plošné spoje).

## A) Plošný spoj CykloPCB v1 (nahradí perfboard i modul TP4056)

| Co | Kolik | Kde | ~Cena |
|---|---|---|---|
| PCB + osazení SMD součástek (Gerber, BOM a CPL jsou v `hardware/pcb/build/`) | 5 desek, 2 osazené | JLCPCB | 30–60 € vč. dopravy a DPH |
| Úhlové tlačítko 6×6 mm **C&K PTS645VL31-2 LFS** | 3 (+2 rezerva) | TME, Mouser | 5× 12 Kč |
| Posuvný přepínač úhlový **C&K OS102011MA1QN1** | 1 (+1) | TME, AliExpress | 2× 15 Kč |
| Konektor **JST PH 2,0 mm 2pin horizontální (S2B-PH-K)** + protikus s kabelem | 1 | TME, AliExpress (sada) | 30 Kč |
| Kolíková lišta 2,54 mm (nebo jen vodiče) | 1 pás | kdekoli | 10 Kč |
| Lanko 0,14–0,25 mm² ve více barvách (silikonové, ohebné) | – | AliExpress, GME | 100 Kč |

> ESP32-C3-Zero, GPS a baterii už máš – přendáš je na PCB.

## A2) Nebo: deska na ruční pájení CykloPCB v1 R (všechno pájíš sám)

Objednáš jen holou desku (`hardware/pcb/build_rucni/…_gerber_JLCPCB.zip`) a
nabíjení dělá **tvůj modul TP4056 USB-C**. Žádné malinké součástky – nejmenší je
1206 (3,2 × 1,6 mm) a SOT-23 s prodlouženými ploškami.
[Návod a zapojení modulu](../hardware/pcb/README.md#varianta-pro-ruční-pájení--cyklopcb-v1-r).

| Co | Kolik | Příklad / kde | ~Cena |
|---|---|---|---|
| Holá deska CykloPCB v1 R | 5 ks (minimum) | JLCPCB, bez osazení | 100–250 Kč vč. dopravy |
| Rezistor 1206 1 % **100k** | 6 (+4) | Yageo RC1206FR-07100KL, GME/TME, nebo sada 1206 | 2 Kč/ks |
| Rezistor 1206 **10k** / **1k** | 1 / 2 (+ rezerva) | Yageo RC1206FR-0710KL / -071KL | 2 Kč/ks |
| Kondenzátor 1206 **10 µF 25 V X5R** | 3 (+2) | Samsung CL31A106KAHNNNE | 4 Kč/ks |
| Kondenzátor 1206 **100 nF 50 V X7R** | 3 (+2) | Samsung CL31B104KBCNNNC | 2 Kč/ks |
| P-MOSFET **AO3401A** (SOT-23) | 3 (+3) | TME: AO3401A; náhrada IRLML6401 | 4–8 Kč/ks |
| Schottky **SK34A / SS34 v pouzdru SMA** | 1 (+1) | TME: SK34A-LTP; jde i SS24A (SMA). Pozor, SS34 v SMB/SMC je větší | 5 Kč |
| Rezistor **2,4 kΩ** 0603 nebo 0805 (volitelně) | 1 | na modul TP4056 místo Rprog 1,2k → nabíjení ~500 mA (méně tepla v krabičce) | 2 Kč |
| LDO **MCP1825S-3302E/DB** (SOT-223) | 1 (+1) | TME, Mouser. **Ne AMS1117** – jiné pořadí vývodů a velký úbytek | 20 Kč |
| Tlačítka, vypínač, JST PH, lišty | jako v tabulce A | | |
| Oboustranná pěnová páska ~1 mm, silikonový drátek 26 AWG | – | GME, TME, LaskaKit | 50–100 Kč |

## B) Krabička a uchycení

| Co | Kolik | ~Cena |
|---|---|---|
| Filament **PETG nebo ASA** (ne PLA – na slunci měkne) | ~60 g | 50 Kč |
| Šrouby: 4× M2.5×14 PT do plastu (ruční deska: 3×), 2× M2×4 PT, 2× M3×25, 2× M3×4 zápustné | sada | 100 Kč |
| Čirý **polykarbonát 1 mm** na ochranné okénko | 5×5 cm | 50 Kč |
| EPDM pěnová páska 3×1 mm (těsnění víčka), oboustranná pěnová páska | – | 80 Kč |
| Silikonové záslepky USB-C | 2 | 50 Kč |
| **Garmin quarter-turn out-front držák** (originál / kompatibilní) | 1 | 100–450 Kč |

## C) Doporučená vylepšení

| Co | Proč | ~Cena |
|---|---|---|
| **Displej 2,4" IPS ST7789 240×320 s pinem BLK** | větší, jas + vypínání podsvícení (viz [doporučení](DOPORUCENI.md)) | 150–300 Kč |
| Li-Po **803450** (8 mm) **s PCM** | o 2 mm tenčí krabička; nebo nech 103450 (delší výdrž) | 150 Kč |
| GPS s modulem **u-blox M10** (např. „M10 GPS mini 18×18“) | rychlejší fix, GPS+Galileo+BeiDou, menší spotřeba | 300–450 Kč |
| **BLE hrudní pás** (Coospo H6, Magene H64…) | tep na displeji a v appce – firmware umí standardní BLE Heart Rate | 500–800 Kč |
| **BLE senzor kadence** (Magene S3+, Coospo BK467…) | kadence – standard BLE Cycling Speed & Cadence | 400–600 Kč |
| Sharp Memory LCD 2,7" (Adafruit 4694) | až budeš chtít „Garmin“ čitelnost na slunci (vyžaduje port firmwaru) | ~1100 Kč |

## D) Pro nepájivé pole (pokud zatím zůstáváš u modulu TP4056)

| Co | Proč | ~Cena |
|---|---|---|
| 2× rezistor 100k + 1× 100 nF | dělič na GPIO0 → měření baterie | 5 Kč |
| Schottky dioda **1N5819** | ochrana: USB do ESP nesmí jít přímo do baterie (viz [doporučení](DOPORUCENI.md#3-lepší-nabíjení-než-modul-tp4056)) | 5 Kč |

## Nářadí (pokud nemáš)

mikropájka s tenkým hrotem, cín s tavidlem 0,5 mm, tavidlo (gel), odsávačka
/ knot, multimetr, posuvné měřítko (na kontrolu rozměrů displeje pro krabičku).
