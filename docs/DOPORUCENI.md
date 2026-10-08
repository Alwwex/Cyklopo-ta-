# Doporučení: aplikace, displej, nabíjení

## 1. Webová aplikace, nebo nativní? → **Webová (PWA) na Androidu**

| | Webová appka (PWA) – `webapp/` | Nativní React Native – `app/` |
|---|---|---|
| Instalace | otevřeš adresu v Chrome → *Přidat na plochu* | build v Android Studiu (JDK 17, Gradle, SDK) |
| Mapy | OpenStreetMap / CyclOSM – zdarma, bez klíče | Google Maps – potřebuje API klíč a platební kartu v Google Cloud |
| Aktualizace | `git push` → hned u všech | nový build + instalace APK |
| Android | ✅ Chrome, Edge, Samsung Internet | ✅ |
| iPhone | ⚠️ jen přes prohlížeč **Bluefy** | ✅ (po úpravách + Mac na build) |
| Běh na pozadí | ❌ prohlížeč appku uspí | ✅ |
| Offline | ✅ (service worker + uložené dlaždice) | ✅ |

**Proč PWA stačí:** jízdu zaznamenává **CykloComp sám** (firmware v2 si každou
jízdu uloží do flash paměti) a appka si ji po otevření stáhne. Takže to, že
prohlížeč appku na pozadí uspí, ničemu nevadí – telefon můžeš mít v kapse
zamčený celou jízdu.

**Kdy by dávala smysl nativní appka:** kdybys chtěl iPhone bez Bluefy, živé
sdílení polohy během jízdy, notifikace na telefonu, automatický upload na Stravu
na pozadí nebo Android Auto. Pro to je v repu nechaná React Native appka (`app/`) –
je kompatibilní s novým firmwarem (nová pole telemetrie ignoruje, nové dlaždice
jsou doplněné), ale dál ji nerozvíjím; nové funkce jsou ve webové appce.

## 2. Lepší obrazovka

Současný 2,0" ST7789 (240×320, IPS) je ostrý a barevný, ale na **přímém slunci**
je každý podsvícený TFT hůř čitelný a podsvícení bere nejvíc proudu.

| Varianta | Pro | Proti | Firmware |
|---|---|---|---|
| **2,4" nebo 2,8" IPS ST7789, 240×320, s pinem BLK** (AliExpress, cca 150–300 Kč) | větší, levný, **funguje hned** (stejný řadič i rozlišení), pin BLK = jas + vypnutí ve spánku (PCB to umí) | na slunci pořád „jen“ IPS | beze změny; v krabičce přepiš `disp_*` |
| **Sharp Memory LCD 2,7" 400×240** (Adafruit 4694, cca 1100 Kč, Botland) | čitelný na slunci jako Garmin, spotřeba v µW (týdny na baterii), vypadá jako Game Boy z videa | černobílý, v noci potřebuje přisvícení, dražší | potřeba port (knihovna Adafruit SharpMem) – RETRO téma je na to připravené |
| **Transflektivní barevný TFT 2,0–2,4"** (ST7789, „sunlight readable“) | barevný a čitelný na slunci | hůř sehnatelný, dražší, často jiné rozměry | beze změny, pokud je ST7789 240×320 |

**Doporučení:** teď kup **2,4" IPS ST7789 s BLK pinem** – na PCB rovnou
funguje řízení jasu i vypínání a na slunci zapni téma **DEN** (bílé pozadí,
maximální kontrast; ve firmwaru dlouhý stisk PAUZA). Až budeš chtít
„Garmin výdrž“, další verze hardwaru = **Sharp Memory LCD**.

Při nákupu hlídej: řadič **ST7789** (ne ILI9341 – to je jiná knihovna),
rozlišení **240×320**, rozhraní **SPI**, piny `GND VCC SCL SDA RES DC CS BLK`.

## 3. Lepší nabíjení než modul TP4056

Co na modulu TP4056 vadí:

1. **Nemá power-path (load sharing).** Když nabíjíš a zároveň jedeš (powerbanka
   na dlouhé jízdě), nabíječka „vidí“ odběr ESP jako baterii, nikdy neukončí
   nabíjení a baterii drží trvale na 4,2 V → rychleji stárne.
2. **Neměří baterii** – ESP neví, kolik zbývá.
3. Ochrana DW01 odpojí až při ~2,4 V – to už článek trpí.
4. Defaultně nabíjí 1 A – pro malé články zbytečně moc a hřeje.

**Řešení na plošném spoji CykloPCB v1 (už hotové):** TP4056 nastavený na
600 mA + **load-sharing** (Schottky dioda + P-MOSFET), **měření baterie** do
GPIO0, **vypínač**, firmware vypne CykloComp při 3,35 V. Takže s PCB už žádný
modul nepotřebuješ.

**Na nepájivé pole / perfboard teď:**

> ⚠️ **Bezpečnost:** pokud máš **OUT+ z TP4056 zapojený přímo do pinu 5V**
> ESP32-C3-Zero, nikdy nepřipojuj USB kabel do ESP, když je baterie zapojená –
> 5 V z USB by šlo přímo do baterie bez nabíjecí elektroniky. Vlož **Schottky
> diodu 1N5819** (anoda na OUT+, katoda na pin 5V). Ztráta 0,3 V nevadí,
> stabilizátor na ESP to zvládne.

- *Měření baterie hned:* **dělič 2× 100k** z OUT+ na **GPIO0** (+ 100 nF z GPIO0
  na GND). Ve firmwaru nastav `#define BAT_ADC_PIN 0` v profilu `BOARD_BREADBOARD`.
- *Hotový modul s power-path* (místo TP4056): **Adafruit bq24074** (PID 4755)
  nebo **Adafruit MCP73871 USB/DC/Solar** (PID 390) – výstup LOAD při nabíjení
  napájí systém z USB a baterii nabíjí zvlášť.
- *Přesné procento:* palivoměr **MAX17048** (I²C, Adafruit 5580 / SparkFun) –
  volitelné, dělič na ADC pro cyklocomputer stačí.

**Nekupuj** modul IP5306 („powerbanka na čipu“) – při malém odběru se sám
vypíná, ESP by ti zhasínal.
