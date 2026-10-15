// MAPA - zivá poloha, projeta stopa, planovani trasy (BRouter), import GPX,
// odeslani trasy + okolnich ulic do CykloCompu.
/* global L */
import { bus, $, toast, openModal, polylineLengthM } from './util.js';
import { state } from './device.js';
import { computeRoute, reducePoints, fetchStreets, flattenSegments, ROUTE_PROFILES } from './routing.js';
import { loadPlan, savePlan, loadSettings, saveSettings } from './storage.js';

export const LAYERS = {
  cyclosm: { label: 'CyclOSM', url: 'https://{s}.tile-cyclosm.openstreetmap.fr/cyclosm/{z}/{x}/{y}.png', attr: '© OpenStreetMap, CyclOSM', max: 20 },
  osm: { label: 'OSM', url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png', attr: '© OpenStreetMap', max: 19 },
  topo: { label: 'Topo', url: 'https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', attr: '© OpenStreetMap, OpenTopoMap', max: 17 },
};

let map, tileLayer, posMarker, riddenLine, routeLine, wpLayer;
let plan = loadPlan();
let planning = false;
let follow = true;
let debounce = null;
let profile = loadSettings().routeProfile;

export function initMap(el) {
  el.innerHTML = `
    <div id="map"></div>
    <div class="map-top">
      <div class="stats">
        <span id="mDist">0<small>km</small></span>
        <span id="mAsc">0<small>m ↑</small></span>
        <span id="mSrc" class="muted small"></span>
        <span style="margin-left:auto"></span>
        <button class="chip" id="mGpx">📂 GPX</button>
      </div>
      <div class="chips" id="mProfiles"></div>
      <div class="elev hidden" id="mElev"></div>
      <input type="file" id="gpxFile" accept=".gpx,application/gpx+xml" class="hidden">
    </div>
    <div class="map-fab">
      <button class="fab" id="fabLayer" title="Vrstva mapy">🗺</button>
      <button class="fab on" id="fabFollow" title="Sledovat polohu">◎</button>
    </div>
    <div class="map-bottom">
      <button class="btn ghost" id="mPlan">✎ Plánovat</button>
      <button class="btn ghost" id="mUndo">↶ Zpět</button>
      <button class="btn ghost" id="mClear">✕ Smazat</button>
      <button class="btn" id="mSend">Odeslat ➜</button>
    </div>`;

  map = L.map($('#map', el), { zoomControl: false, attributionControl: true }).setView([50.0755, 14.4378], 13);
  setLayer(loadSettings().mapLayer);
  wpLayer = L.layerGroup().addTo(map);
  routeLine = L.polyline([], { color: '#1e6cf0', weight: 6, opacity: 0.9 }).addTo(map);
  riddenLine = L.polyline([], { color: '#ff7a1a', weight: 5 }).addTo(map);
  posMarker = L.marker([0, 0], { icon: L.divIcon({ className: '', html: '<div class="pos-marker"></div>', iconSize: [22, 22], iconAnchor: [11, 11] }), interactive: false });

  map.on('click', (e) => {
    if (!planning) return;
    plan.waypoints.push({ lat: e.latlng.lat, lng: e.latlng.lng });
    planChanged();
  });
  map.on('dragstart', () => { follow = false; $('#fabFollow').classList.remove('on'); });

  $('#mPlan', el).onclick = () => {
    planning = !planning;
    $('#mPlan').textContent = planning ? '✓ Hotovo' : '✎ Plánovat';
    $('#mPlan').classList.toggle('on', planning);
    if (planning) { follow = false; $('#fabFollow').classList.remove('on'); toast('Klepej do mapy a přidávej body trasy'); }
  };
  $('#mUndo', el).onclick = () => { plan.waypoints.pop(); planChanged(); };
  $('#mClear', el).onclick = () => {
    plan = { waypoints: [], route: [], distanceM: 0, ascentM: 0, elevations: [] };
    savePlan(plan); drawPlan();
    if (state.conn === 'connected') state.dev.send('RT:CLEAR').catch(() => {});
  };
  $('#mSend', el).onclick = sendToDevice;
  $('#fabFollow', el).onclick = () => {
    follow = !follow;
    $('#fabFollow').classList.toggle('on', follow);
    if (follow && state.tel?.fix) map.setView([state.tel.lat, state.tel.lng], Math.max(map.getZoom(), 15));
  };
  $('#fabLayer', el).onclick = () => {
    const keys = Object.keys(LAYERS);
    const s = loadSettings();
    const next = keys[(keys.indexOf(s.mapLayer) + 1) % keys.length];
    saveSettings({ ...s, mapLayer: next });
    setLayer(next);
    toast(`Mapa: ${LAYERS[next].label}`);
  };
  $('#mGpx', el).onclick = () => $('#gpxFile').click();
  $('#gpxFile', el).onchange = importGpx;

  renderProfiles();
  drawPlan();
  if (plan.route.length > 1) map.fitBounds(L.latLngBounds(plan.route.map((p) => [p.lat, p.lng])), { padding: [40, 140] });

  bus.on('telemetry', (t) => {
    if (t.fix !== 1) return;
    posMarker.setLatLng([t.lat, t.lng]);
    if (!map.hasLayer(posMarker)) posMarker.addTo(map);
    riddenLine.setLatLngs(state.ridden);
    if (follow && !planning && el.offsetParent) map.panTo([t.lat, t.lng], { animate: true });
  });
}

export function onShowMap() {
  setTimeout(() => {
    map.invalidateSize();
    if (follow && state.tel?.fix) map.setView([state.tel.lat, state.tel.lng], Math.max(map.getZoom(), 15));
  }, 50);
}

function setLayer(key) {
  const l = LAYERS[key] || LAYERS.cyclosm;
  if (tileLayer) map.removeLayer(tileLayer);
  tileLayer = L.tileLayer(l.url, { maxZoom: l.max, attribution: l.attr, subdomains: 'abc', crossOrigin: true }).addTo(map);
}

function renderProfiles() {
  const box = $('#mProfiles');
  box.innerHTML = ROUTE_PROFILES.map((p) => `<button class="chip ${p.id === profile ? 'on' : ''}" data-p="${p.id}">${p.label}</button>`).join('');
  box.querySelectorAll('[data-p]').forEach((b) => {
    b.onclick = () => {
      profile = b.dataset.p;
      saveSettings({ ...loadSettings(), routeProfile: profile });
      renderProfiles();
      planChanged();
    };
  });
}

function planChanged() {
  drawPlan();
  clearTimeout(debounce);
  debounce = setTimeout(recompute, 700);
}

async function recompute() {
  if (plan.waypoints.length < 2) {
    plan.route = []; plan.distanceM = 0; plan.ascentM = 0; plan.elevations = [];
    savePlan(plan); drawPlan();
    return;
  }
  $('#mSrc').textContent = 'počítám…';
  try {
    const r = await computeRoute(plan.waypoints, profile);
    Object.assign(plan, { route: r.coords, distanceM: r.distanceM, ascentM: r.ascentM, elevations: r.elevations, source: r.source });
  } catch (e) {
    toast(`Trasu se nepodařilo spočítat: ${e.message}`, 4000);
    // nouzove: rovne cary mezi body
    Object.assign(plan, { route: plan.waypoints.slice(), distanceM: polylineLengthM(plan.waypoints), ascentM: 0, elevations: [], source: 'přímo' });
  }
  savePlan(plan);
  drawPlan();
}

function drawPlan() {
  wpLayer.clearLayers();
  plan.waypoints.forEach((w, i) => {
    const cls = i === 0 ? 'start' : i === plan.waypoints.length - 1 ? 'end' : '';
    const m = L.marker([w.lat, w.lng], {
      draggable: true,
      icon: L.divIcon({ className: '', html: `<div class="wp-marker ${cls}"></div>`, iconSize: [18, 18], iconAnchor: [9, 9] }),
    });
    m.on('dragend', () => { const p = m.getLatLng(); plan.waypoints[i] = { lat: p.lat, lng: p.lng }; planChanged(); });
    wpLayer.addLayer(m);
  });
  routeLine.setLatLngs(plan.route.map((p) => [p.lat, p.lng]));
  $('#mDist').innerHTML = `${(plan.distanceM / 1000).toFixed(1)}<small>km</small>`;
  $('#mAsc').innerHTML = `${Math.round(plan.ascentM || 0)}<small>m ↑</small>`;
  $('#mSrc').textContent = plan.source && plan.route.length ? plan.source : '';
  drawElevation();
}

function drawElevation() {
  const box = $('#mElev');
  const el = (plan.elevations || []).filter((v) => v != null);
  if (el.length < 2) { box.classList.add('hidden'); return; }
  box.classList.remove('hidden');
  const min = Math.min(...el), max = Math.max(...el);
  const span = Math.max(10, max - min);
  const step = Math.max(1, Math.floor(el.length / 200));
  const pts = [];
  for (let i = 0; i < el.length; i += step) pts.push(`${((i / (el.length - 1)) * 300).toFixed(1)},${(52 - ((el[i] - min) / span) * 46).toFixed(1)}`);
  box.innerHTML = `<svg viewBox="0 0 300 54" preserveAspectRatio="none">
    <polygon points="0,54 ${pts.join(' ')} 300,54" fill="var(--accent)" opacity=".25"/>
    <polyline points="${pts.join(' ')}" fill="none" stroke="var(--accent)" stroke-width="1.5" vector-effect="non-scaling-stroke"/>
    <text x="3" y="11" font-size="9" fill="var(--muted)">${Math.round(max)} m</text>
    <text x="3" y="51" font-size="9" fill="var(--muted)">${Math.round(min)} m</text></svg>`;
}

async function importGpx(e) {
  const file = e.target.files[0];
  e.target.value = '';
  if (!file) return;
  const xml = new DOMParser().parseFromString(await file.text(), 'application/xml');
  const nodes = [...xml.querySelectorAll('trkpt, rtept')];
  if (nodes.length < 2) { toast('V GPX není žádná trasa'); return; }
  const route = nodes.map((n) => ({ lat: +n.getAttribute('lat'), lng: +n.getAttribute('lon') }));
  const elevations = nodes.map((n) => { const e2 = n.querySelector('ele'); return e2 ? +e2.textContent : null; });
  let asc = 0;
  for (let i = 1; i < elevations.length; i++) if (elevations[i] != null && elevations[i - 1] != null && elevations[i] > elevations[i - 1]) asc += elevations[i] - elevations[i - 1];
  plan = { waypoints: [route[0], route[route.length - 1]], route, distanceM: polylineLengthM(route), ascentM: asc, elevations, source: file.name };
  savePlan(plan);
  drawPlan();
  map.fitBounds(L.latLngBounds(route.map((p) => [p.lat, p.lng])), { padding: [40, 140] });
  toast(`Načteno ${route.length} bodů z GPX`);
}

async function sendToDevice() {
  if (plan.route.length < 2) { toast('Nejdřív naplánuj trasu (✎ Plánovat) nebo načti GPX'); return; }
  if (state.conn !== 'connected') { toast('CykloComp není připojen'); return; }
  const m = openModal(`<h2>Odesílám do CykloCompu</h2><p class="muted" id="sendStep">Trasa…</p><div class="progress"><div id="sendBar"></div></div>`);
  const bar = (p, txt) => { $('#sendBar').style.width = `${Math.round(p * 100)}%`; if (txt) $('#sendStep').textContent = txt; };
  try {
    await state.dev.send('RT:CLEAR');
    await state.dev.sendPoints(reducePoints(plan.route, 280), 'RT:P:', 'RT:BEGIN', 'RT:END', (p) => bar(p * 0.4, 'Trasa…'));
    bar(0.45, 'Stahuji ulice z OpenStreetMap…');
    let streets = [];
    try { streets = flattenSegments(await fetchStreets(plan.route)); } catch (e) { console.warn(e); }
    if (streets.length) await state.dev.sendPoints(streets, 'MP:P:', 'MP:BEGIN', 'MP:END', (p) => bar(0.5 + p * 0.5, `Ulice (${streets.length} bodů)…`));
    m.close();
    toast(streets.length ? 'Trasa i ulice jsou v CykloCompu ✓' : 'Trasa odeslána ✓ (ulice se nepodařilo stáhnout)', 3500);
  } catch (e) {
    m.close();
    toast(`Odeslání selhalo: ${e.message || e}`, 4000);
  }
}
