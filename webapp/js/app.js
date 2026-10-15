// CykloComp web app - vstupni bod: taby, pripojeni, tema, service worker.
import { bus, $, $$, openModal } from './util.js';
import { state, connect, disconnect, tryAutoConnect } from './device.js';
import { applyTheme } from './theme.js';
import { initRide } from './view-ride.js';
import { initMap, onShowMap } from './view-map.js';
import { initHistory } from './view-history.js';
import { initProfile, render as renderProfile } from './view-profile.js';
import { initSettings } from './view-settings.js';

const views = {
  ride: { el: $('#v-ride'), init: initRide },
  map: { el: $('#v-map'), init: initMap, show: onShowMap },
  history: { el: $('#v-history'), init: initHistory },
  profile: { el: $('#v-profile'), init: initProfile, show: renderProfile },
  settings: { el: $('#v-settings'), init: initSettings },
};

function showTab(name) {
  Object.entries(views).forEach(([k, v]) => v.el.classList.toggle('hidden', k !== name));
  $$('.tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  views[name].show && views[name].show();
  history.replaceState(null, '', `#${name}`);
}

function renderConnChip(c) {
  const chip = $('#connChip');
  const label = c === 'connected' ? (state.dev?.name || 'CykloComp') : c === 'connecting' ? 'Připojuji…' : 'Nepřipojeno';
  chip.innerHTML = `<span class="dot ${c === 'connected' ? 'on' : c === 'connecting' ? 'busy' : ''}"></span>${label}`;
}

$('#connChip').onclick = () => {
  if (state.conn === 'connected') {
    const m = openModal(`<h2>${state.dev?.name || 'CykloComp'}</h2>
      <p class="muted">Firmware ${state.status?.fw || '?'} · baterie ${state.tel?.bat > 100 ? 'USB' : `${state.tel?.bat ?? '--'} %`}</p>
      <div class="row"><button class="btn ghost" data-close>Zavřít</button><button class="btn danger" id="mDisc">Odpojit</button></div>`);
    $('#mDisc', m.root).onclick = () => { m.close(); disconnect(); };
  } else if (state.conn !== 'connecting') {
    showTab('ride');
  }
};

Object.values(views).forEach((v) => v.init(v.el));
$$('.tabs button').forEach((b) => { b.onclick = () => showTab(b.dataset.tab); });
bus.on('connection', renderConnChip);
renderConnChip(state.conn);
applyTheme();

const start = location.hash.slice(1);
showTab(views[start] ? start : 'ride');

if (new URLSearchParams(location.search).has('demo')) connect('demo');
else tryAutoConnect();

if ('serviceWorker' in navigator && location.protocol !== 'file:') {
  navigator.serviceWorker.register('sw.js').catch((e) => console.warn('SW', e));
}
