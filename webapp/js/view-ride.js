// JIZDA - palubni deska ve stylu displeje (oblouk rychlosti + dlazdice).
import { bus, $, fmtTime, fmtNum, cssVar, lerpColor, openModal } from './util.js';
import { state, connect, send } from './device.js';
import { BleDevice } from './ble.js';

const SEGS = 45;
const CX = 120, CY = 118, R_OUT = 110, R_IN = 94;
let root;
let segEls = [];
let gaugeMax = 60;
let litCount = -1;
let wakeLock = null;

function segPath(i) {
  const a0 = ((135 + i * 6 + 0.6) * Math.PI) / 180;
  const a1 = ((135 + i * 6 + 5.4) * Math.PI) / 180;
  const p = (r, a) => `${(CX + Math.cos(a) * r).toFixed(1)},${(CY + Math.sin(a) * r).toFixed(1)}`;
  return `M${p(R_IN, a0)} L${p(R_OUT, a0)} L${p(R_OUT, a1)} L${p(R_IN, a1)} Z`;
}

function markerPath(kmh) {
  const a = ((135 + (270 * Math.min(kmh, gaugeMax)) / gaugeMax) * Math.PI) / 180;
  const c = Math.cos(a), s = Math.sin(a);
  const tip = [CX + c * 90, CY + s * 90], base = [CX + c * 80, CY + s * 80];
  const px = -s * 5, py = c * 5;
  return `M${tip[0]},${tip[1]} L${base[0] + px},${base[1] + py} L${base[0] - px},${base[1] - py} Z`;
}

function gaugeSvg() {
  let segs = '';
  for (let i = 0; i < SEGS; i++) segs += `<path class="seg" d="${segPath(i)}"/>`;
  let ticks = '';
  for (let v = 0; v <= gaugeMax; v += gaugeMax > 45 ? 10 : 5) {
    const a = ((135 + (270 * v) / gaugeMax) * Math.PI) / 180;
    ticks += `<circle cx="${CX + Math.cos(a) * 76}" cy="${CY + Math.sin(a) * 76}" r="1.6" fill="var(--muted)"/>`;
    const lx = CX + Math.cos(a) * 64, ly = CY + Math.sin(a) * 64 + 3;
    if (v === 0 || v === gaugeMax || v === gaugeMax / 2) ticks += `<text x="${lx}" y="${ly}" font-size="9" fill="var(--muted)" text-anchor="middle">${v}</text>`;
  }
  return `<svg viewBox="0 0 240 206" aria-label="Rychlost">
    <g id="segs">${segs}</g>${ticks}
    <path id="mkMax" fill="var(--danger)" d=""/>
    <path id="mkAvg" fill="var(--accent)" d=""/>
  </svg>`;
}

const TILES = [
  ['dst', 'Vzdálenost', 'km'], ['avg', 'Průměr', 'km/h'], ['max', 'Max', 'km/h'],
  ['asc', 'Stoupání', 'm'], ['alt', 'Výška', 'm'], ['grd', 'Sklon', '%'],
  ['hr', 'Tep', 'bpm'], ['cad', 'Kadence', 'rpm'], ['bat', 'Baterie', '%'],
];

export function initRide(el) {
  root = el;
  const bleOk = BleDevice.supported();
  root.innerHTML = `
  <div class="wrap">
    <section id="rideOffline" class="hero">
      <svg viewBox="0 0 120 80" width="150" aria-hidden="true">
        <path d="M14 66 A46 46 0 1 1 106 66" fill="none" stroke="var(--track)" stroke-width="10"/>
        <path d="M14 66 A46 46 0 0 1 60 20" fill="none" stroke="var(--accent)" stroke-width="10"/>
        <text x="60" y="64" text-anchor="middle" font-family="DSEG7" font-size="22" fill="var(--text)">24.5</text>
      </svg>
      <h2>Připoj svůj CykloComp</h2>
      <p>${bleOk ? 'Zapni CykloComp a klepni na Připojit. Telefon si ho příště najde sám.'
        : 'Tento prohlížeč neumí Bluetooth (Web Bluetooth). Na Androidu otevři appku v <b>Chrome</b>. Na iPhonu použij prohlížeč <b>Bluefy</b>. Demo funguje všude.'}</p>
      <button class="btn" id="btnConnect" ${bleOk ? '' : 'disabled'}>Připojit CykloComp</button>
      <div style="height:10px"></div>
      <button class="btn ghost" id="btnDemo">Vyzkoušet demo (bez hardwaru)</button>
    </section>

    <section id="rideOnline" class="hidden">
      <div class="status-strip" id="strip"></div>
      <div class="gauge-wrap">
        ${gaugeSvg()}
        <div class="speed">
          <div class="msg" id="spMsg"></div>
          <div class="num" id="spNum"><span class="ghost">88.8</span><span class="val" id="spVal">0.0</span></div>
          <div class="unit">km/h</div>
        </div>
        <div class="ride-time" id="rideTime">0:00</div>
      </div>
      <div class="tiles">
        ${TILES.map(([k, l, u]) => `<div class="tile"><div class="l">${l}</div><div class="v"><span id="t_${k}">--</span><small>${u}</small></div></div>`).join('')}
      </div>
      <div class="ride-actions" id="actions"></div>
      <div class="row" style="margin-top:10px">
        <button class="btn ghost small" id="btnWake">☀ Nezhasínat displej</button>
        <button class="btn ghost small" id="btnScreen">⇆ Obrazovka CykloCompu</button>
      </div>
    </section>
  </div>`;

  segEls = [...root.querySelectorAll('.seg')];
  $('#btnConnect', root).onclick = () => connect('ble');
  $('#btnDemo', root).onclick = () => connect('demo');
  $('#btnWake', root).onclick = toggleWake;
  $('#btnScreen', root).onclick = () => {
    const scr = ((state.status?.scr ?? 0) + 1) % 4;
    send(`SCREEN:${scr}`).catch(() => {});
    if (state.status) state.status.scr = scr;
  };

  bus.on('connection', renderConn);
  bus.on('telemetry', renderTel);
  bus.on('status', (s) => {
    if (s.gmax && s.gmax !== gaugeMax) { gaugeMax = s.gmax; rebuildGauge(); }
    $('#t_grd').textContent = fmtNum(s.grd, 1);
  });
  bus.on('theme', () => { litCount = -1; if (state.tel) renderTel(state.tel); });
  renderConn(state.conn);
}

function rebuildGauge() {
  const wrap = $('.gauge-wrap', root);
  wrap.querySelector('svg').outerHTML = gaugeSvg();
  segEls = [...root.querySelectorAll('.seg')];
  litCount = -1;
}

function renderConn(c) {
  const online = c === 'connected' || (c === 'connecting' && state.tel);
  $('#rideOffline', root).classList.toggle('hidden', online);
  $('#rideOnline', root).classList.toggle('hidden', !online);
  $('#btnConnect', root).textContent = c === 'connecting' ? 'Připojuji…' : 'Připojit CykloComp';
  if (online && state.tel) renderTel(state.tel);
  else if (online) renderActions(false, false);
}

function segColor(i) {
  const t = i / (SEGS - 1);
  const lo = cssVar('--g-lo'), mid = cssVar('--g-mid'), hi = cssVar('--g-hi');
  return t < 0.5 ? lerpColor(lo, mid, t * 2) : lerpColor(mid, hi, (t - 0.5) * 2);
}

function renderGauge(kmh) {
  const n = Math.max(0, Math.min(SEGS, Math.round((kmh / gaugeMax) * SEGS)));
  if (n === litCount) return;
  const track = cssVar('--track');
  segEls.forEach((el, i) => { el.setAttribute('fill', i < n ? segColor(i) : track); });
  litCount = n;
}

function renderTel(t) {
  if (!root) return;
  const fix = t.fix === 1;
  const v = fix ? t.spd : 0;
  renderGauge(v);
  const avg = t.tim > 0 ? t.dst / (t.tim / 3600) : 0;
  $('#mkAvg', root).setAttribute('d', t.rid || t.dst > 0.1 ? markerPath(avg) : '');
  $('#mkMax', root).setAttribute('d', t.max > 1 ? markerPath(t.max) : '');

  const txt = Math.min(v, 99.9).toFixed(1);
  $('#spVal', root).textContent = txt.length < 4 ? `!${txt}` : txt; // "!" = prazdna pozice v DSEG7
  $('#spNum', root).classList.toggle('nofix', !fix);
  const apa = state.status?.apa === 1;
  $('#spMsg', root).textContent = !fix ? 'HLEDÁM GPS' : t.pau ? 'PAUZA' : (t.rid && apa) ? 'AUTOPAUZA' : (state.status?.sim ? 'SIMULACE' : '');

  const rt = $('#rideTime', root);
  rt.textContent = fmtTime(t.tim);
  rt.className = `ride-time ${t.rid ? (t.pau || apa ? 'paused' : 'rec') : ''}`;

  $('#t_dst').textContent = t.dst.toFixed(2);
  $('#t_avg').textContent = avg.toFixed(1);
  $('#t_max').textContent = fmtNum(t.max, 1);
  $('#t_asc').textContent = t.asc != null ? Math.round(t.asc) : '--';
  $('#t_alt').textContent = fix ? Math.round(t.alt) : '--';
  $('#t_hr').textContent = t.hr ? t.hr : '--';
  $('#t_cad').textContent = t.cad ? t.cad : '--';
  $('#t_bat').textContent = t.bat == null || t.bat < 0 ? '--' : t.bat > 100 ? 'USB' : t.bat;

  const strip = $('#strip', root);
  strip.innerHTML = `
    ${t.rid ? (t.pau ? '<span class="badge pause">❚❚ PAUZA</span>' : '<span class="badge rec">ZÁZNAM</span>') : '<span class="badge">STOP</span>'}
    <span class="badge">🛰 ${fix ? `${t.sat} sat` : 'hledám'}</span>
    ${state.dev?.kind === 'demo' ? '<span class="badge">DEMO</span>' : ''}
    ${state.syncing ? '<span class="badge">⇅ synchronizuji</span>' : ''}`;
  renderActions(t.rid === 1, t.pau === 1);
}

let lastActions = '';
function renderActions(rec, paused) {
  const key = `${rec}-${paused}`;
  if (key === lastActions) return;
  lastActions = key;
  const a = $('#actions', root);
  if (!rec) {
    a.innerHTML = '<button class="btn" id="aStart">▶ ZAČÍT JÍZDU</button>';
    $('#aStart', a).onclick = () => send('RIDE:START').catch(() => {});
  } else {
    a.innerHTML = `<div class="row">
      <button class="btn ghost" id="aPause">${paused ? '▶ POKRAČOVAT' : '❚❚ PAUZA'}</button>
      <button class="btn danger" id="aStop">■ UKONČIT</button></div>`;
    $('#aPause', a).onclick = () => send(paused ? 'RIDE:RESUME' : 'RIDE:PAUSE').catch(() => {});
    $('#aStop', a).onclick = () => {
      const m = openModal(`<h2>Ukončit jízdu?</h2><p class="muted">Jízda se uloží v CykloCompu a stáhne do historie.</p>
        <div class="row"><button class="btn ghost" data-close>Zpět</button><button class="btn danger" id="cfStop">Ukončit</button></div>`);
      $('#cfStop', m.root).onclick = () => { m.close(); send('RIDE:STOP').catch(() => {}); };
    };
  }
}

async function toggleWake() {
  const b = $('#btnWake', root);
  try {
    if (wakeLock) { await wakeLock.release(); wakeLock = null; }
    else if ('wakeLock' in navigator) {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; b.classList.remove('on'); b.textContent = '☀ Nezhasínat displej'; });
    }
  } catch (e) { console.warn(e); }
  b.textContent = wakeLock ? '☀ Displej nezhasne ✓' : '☀ Nezhasínat displej';
}
