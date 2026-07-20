# CykloComp – mobilní aplikace (React Native, Android)

Tento adresář obsahuje **JS zdrojový kód aplikace** (obrazovky, BLE vrstva,
gamifikace, služby pro routing/OSM). RN CLI generuje nativní `android/`
a `ios/` složky samo – ty se do gitu obvykle needitují ručně, proto tu
nejsou předgenerované. Postup, jak z tohoto zdrojáku dostat spustitelnou
appku:

## 1. Vytvoř nativní shell

```bash
npx @react-native-community/cli@latest init CykloCompApp --version 0.74.5
```

Tím vznikne nová složka `CykloCompApp/` s `android/` a `ios/`. Zkopíruj
do ní (přepiš) soubory z tohoto `app/` adresáře: `App.js`, `index.js`,
`app.json`, `babel.config.js`, `package.json` (sluč závislosti) a celou
složku `src/`.

## 2. Nainstaluj závislosti

```bash
npm install
```

Knihovny: `react-native-ble-plx`, `react-native-maps`,
`@react-native-async-storage/async-storage`, `@react-navigation/*`,
`buffer`.

## 3. Android konfigurace

- **JDK 17** (ne novější – novější verze lámou Gradle build RN 0.74).
- `ANDROID_HOME` a `platform-tools` v `PATH`.
- Zkopíruj oprávnění z `android-manifest-snippets/AndroidManifest-permissions.xml`
  do `android/app/src/main/AndroidManifest.xml` (`<uses-permission>` tagy
  před `<application>`, `<meta-data>` s Google Maps API klíčem dovnitř
  `<application>`).
- Google Maps API klíč získáš v Google Cloud Console (Maps SDK for Android).

## 4. Spuštění

```bash
npx react-native run-android
```

Telefon připoj přes USB s povoleným USB debuggingem. **BLE nefunguje
v emulátoru** – nutné reálné Android zařízení.

## Struktura `src/`

| Cesta | Účel |
|---|---|
| `theme.js` | Design system (barvy, typografie, spacing) |
| `ble/BleManager.js` | Singleton BLE vrstva (sken, MTU 185, reconnect po reloadu, telemetrie, chunk. odesílání trasy/ulic) |
| `ble/uuids.js` | UUID service/charakteristik – musí sedět s firmwarem |
| `ble/permissions.js` | Runtime žádost o BLE oprávnění dle verze Androidu |
| `storage/storage.js` | AsyncStorage: jízdy, profil, konfigurace polí, naplánovaná trasa |
| `gamification/gamification.js` | Čistá logika bodů/levelů/streaku/odznaků (bez UI) |
| `services/overpass.js` | Ulice z OSM (Overpass API) |
| `services/routing.js` | Trasa po cestách (BRouter → OSRM fallback) |
| `screens/*.js` | 4 obrazovky (Jízda/Mapa/Historie/Profil) |
| `navigation/AppNavigator.js` | Spodní taby |

## Známé pasti (viz kód pro detaily)

1. `device.connect({ requestMTU: 185 })` je **povinné** – jinak Android
   nechá MTU 23 B a JSON telemetrie se tiše ořízne.
2. Po reloadu appky Android drží staré BLE spojení – před skenem se
   vždy nejdřív zkouší `connectedDevices([SERVICE_UUID])`.
3. V `MapScreen.handleMapPress` se souřadnice z `event.nativeEvent`
   vytahuje **synchronně před** voláním `setState` – React syntetické
   eventy se recyklují a uvnitř updater funkce by `nativeEvent` už
   mohl být `null`.
4. Overpass hlavní server (`overpass-api.de`) běžně vrací 406 bez
   vlastního `User-Agent` a bez `POST` s `text/plain` tělem – proto se
   zkouší zrcadla v pořadí `kumi.systems → z.overpass-api.de → overpass-api.de`.
