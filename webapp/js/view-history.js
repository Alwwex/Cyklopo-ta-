// HISTORIE - jizdy stazene z CykloCompu, detail s mapou, export GPX pro Stravu.
/* global L */
import { bus, $, fmtTime, fmtDate, toast, openModal, download } from './util.js';
import { loadRides, deleteRide } from './storage.js';
import { state, syncRides } from './device.js';
import { toGPX } from './routing.js';
import { LAYERS } from './view-map.js';

let root;

function miniTrack(track) {
  if (!track || track.length < 2) return '';
  let s = 90, n = -90, w = 180, e = -180;
  for (const [la, lo] of track) { s = Math.min(s, la); n = Math.max(n, la); w = Math.min(w, lo); e = Math.max(e, lo); }
  const kx = Math.cos(((s + n) / 2) * Math.PI / 180);
  const W = 300, H = 70, pad = 6;
  const sx = (e - w) * kx || 1e-6, sy = (n - s) || 1e-6;
  const k = Math.min((W - 2 * pad) / sx, (H - 2 * pad) / sy);
  const ox = (W - sx * k) / 2, oy = (H - sy * k) / 2;
  const step = Math.max(1, Math.floor(track.length / 150));
  const pts = [];
  for (let i = 0; i < track.length; i += step) pts.push(`${(ox + (track[i][1] - w) * kx * k).toFixed(1)},${(H - oy - (track[i][0] - s) * k).toFixed(1)}`);
  return `<svg class="mini-track" viewBox="0 0 ${W} ${H}"><polyline points="${pts.join(' ')}" fill="none" stroke="var(--accent)" stroke-width="2.5" stroke-linejoin="round" stroke-linecap="round"/></svg>`;
}

export function initHistory(el) {
  root = el;
  root.innerHTML = `<div class="wrap">
    <div class="row" style="margin-bottom:12px">
      <button class="btn" id="hSync">⇅ Stáhnout jízdy z CykloCompu</button>
    </div>
    <div id="hList"></div></div>`;
  $('#hSync', root).onclick = () => {
    if (state.conn !== 'connected') { toast('Nejdřív připoj CykloComp'); return; }
    syncRides({ quiet: false });
  };
  bus.on('rides-changed', render);
  bus.on('sync', (s) => {
    const b = $('#hSync', root);
    b.disabled = s.active;
    b.textContent = s.active ? `Stahuji… ${Math.round(s.progress * 100)} %` : '⇅ Stáhnout jízdy z CykloCompu';
  });
  render();
}

async function render() {
  const rides = await loadRides();
  const list = $('#hList', root);
  if (!rides.length) {
    list.innerHTML = '<div class="empty">Zatím žádné jízdy.<br>Odjeď jízdu s CykloCompem a po připojení se sama stáhne sem.</div>';
    return;
  }
  list.innerHTML = rides.map((r) => `
    <button class="panel ride-card" data-id="${r.id}">
      <div class="head"><span class="date">${fmtDate(r.dateISO)}</span><span class="pts">+${r.points || 0} b.</span></div>
      <div class="grid">
        <div><b>${r.distanceKm.toFixed(1)}</b><span>km</span></div>
        <div><b>${fmtTime(r.timeS)}</b><span>čas</span></div>
        <div><b>${(r.avgKmh || 0).toFixed(1)}</b><span>prům.</span></div>
        <div><b>${Math.round(r.ascentM || 0)}</b><span>m ↑</span></div>
      </div>
      ${miniTrack(r.track)}
    </button>`).join('');
  list.querySelectorAll('[data-id]').forEach((b) => { b.onclick = () => openDetail(rides.find((r) => r.id === b.dataset.id)); });
}

function openDetail(r) {
  const m = openModal(`
    <h2>${fmtDate(r.dateISO)}</h2>
    <p class="muted small">${new Date(r.dateISO).toLocaleTimeString('cs-CZ', { hour: '2-digit', minute: '2-digit' })} · ${r.source === 'device' ? 'z CykloCompu' : 'z aplikace'}</p>
    <div id="rideMap"></div>
    <div class="tiles">
      <div class="tile"><div class="l">Vzdálenost</div><div class="v">${r.distanceKm.toFixed(2)}<small>km</small></div></div>
      <div class="tile"><div class="l">Čas</div><div class="v">${fmtTime(r.timeS)}</div></div>
      <div class="tile"><div class="l">Průměr</div><div class="v">${(r.avgKmh || 0).toFixed(1)}<small>km/h</small></div></div>
      <div class="tile"><div class="l">Max</div><div class="v">${(r.maxKmh || 0).toFixed(1)}<small>km/h</small></div></div>
      <div class="tile"><div class="l">Stoupání</div><div class="v">${Math.round(r.ascentM || 0)}<small>m</small></div></div>
      <div class="tile"><div class="l">Body</div><div class="v">${r.points || 0}</div></div>
    </div>
    <div class="row" style="margin-top:14px">
      <button class="btn ghost" id="dDel">🗑 Smazat</button>
      <button class="btn" id="dGpx" ${r.track?.length > 1 ? '' : 'disabled'}>⇩ GPX (Strava)</button>
    </div>
    <div style="height:8px"></div><button class="btn ghost" data-close>Zavřít</button>`);
  setTimeout(() => {
    const el = $('#rideMap', m.root);
    if (!r.track || r.track.length < 2) { el.innerHTML = '<div class="empty">Bez stopy</div>'; return; }
    const mm = L.map(el, { zoomControl: false, attributionControl: false });
    L.tileLayer(LAYERS.cyclosm.url, { subdomains: 'abc', maxZoom: 19 }).addTo(mm);
    const line = L.polyline(r.track, { color: '#ff7a1a', weight: 5 }).addTo(mm);
    L.circleMarker(r.track[0], { radius: 7, color: '#fff', fillColor: '#3cd264', fillOpacity: 1, weight: 2 }).addTo(mm);
    L.circleMarker(r.track[r.track.length - 1], { radius: 7, color: '#fff', fillColor: '#111', fillOpacity: 1, weight: 2 }).addTo(mm);
    mm.fitBounds(line.getBounds(), { padding: [16, 16] });
  }, 60);
  $('#dGpx', m.root).onclick = () => {
    const name = `CykloComp ${new Date(r.dateISO).toLocaleDateString('cs-CZ')}`;
    download(`cyklocomp-${r.dateISO.slice(0, 10)}.gpx`, toGPX(name, r.track, r.dateISO), 'application/gpx+xml');
  };
  $('#dDel', m.root).onclick = async () => {
    if (!confirm('Opravdu smazat tuto jízdu?')) return;
    await deleteRide(r.id);
    m.close();
    render();
    toast('Jízda smazána');
  };
}
