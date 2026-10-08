// Service worker: aplikace funguje offline (i v terenu bez signalu),
// mapove dlazdice ktere uz jsi videl se pamatuji.
const VERSION = 'cc-v2.0.0';
const APP = [
  './', 'index.html', 'manifest.webmanifest', 'css/app.css',
  'js/app.js', 'js/util.js', 'js/ble.js', 'js/demo.js', 'js/device.js', 'js/storage.js', 'js/theme.js',
  'js/gamification.js', 'js/routing.js', 'js/view-ride.js', 'js/view-map.js', 'js/view-history.js',
  'js/view-profile.js', 'js/view-settings.js',
  'vendor/leaflet/leaflet.js', 'vendor/leaflet/leaflet.css', 'vendor/fonts/DSEG7Classic-Bold.woff2',
  'icons/icon.svg', 'icons/icon-192.png', 'icons/icon-512.png',
];
const TILE_CACHE = 'cc-tiles';
const MAX_TILES = 1500;

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(APP)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k !== VERSION && k !== TILE_CACHE).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

const isTile = (url) => /tile|cyclosm|opentopomap/.test(url.hostname) && /\/\d+\/\d+\/\d+\.png$/.test(url.pathname);

async function trimTiles() {
  const c = await caches.open(TILE_CACHE);
  const keys = await c.keys();
  for (let i = 0; i < keys.length - MAX_TILES; i++) await c.delete(keys[i]);
}

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET') return;
  if (isTile(url)) {
    e.respondWith(caches.open(TILE_CACHE).then(async (c) => {
      const hit = await c.match(e.request);
      if (hit) return hit;
      const res = await fetch(e.request);
      if (res.ok || res.type === 'opaque') { c.put(e.request, res.clone()); trimTiles(); }
      return res;
    }));
    return;
  }
  if (url.origin === location.origin) {
    // aplikace: nejdriv sit (at se aktualizace projevi hned), bez site cache
    e.respondWith(fetch(e.request)
      .then((res) => { const copy = res.clone(); caches.open(VERSION).then((c) => c.put(e.request, copy)); return res; })
      .catch(() => caches.match(e.request, { ignoreSearch: true })));
  }
});
