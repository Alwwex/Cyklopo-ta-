// NASTAVENI - nastaveni CykloCompu (pres BLE) a aplikace.
import { bus, $, $$, toast, download } from './util.js';
import { state, send, disconnect } from './device.js';
import { loadSettings, saveSettings, loadRides, saveRide, clearRides, loadProfile, saveProfile } from './storage.js';
import { applyTheme } from './theme.js';

export const FIELD_OPTIONS = ['TRIP', 'ČAS', 'PRŮMĚR', 'MAX', 'VÝŠKA', 'ODOMETR', 'SATELITY', 'HODINY',
  'STOUPÁNÍ', 'SKLON', 'TEP', 'KADENCE', 'BATERIE', 'DO CÍLE'];
const THEMES = [
  { id: 0, key: 'noc', label: 'Noc', bg: '#04080f', fg: '#ff7a1a' },
  { id: 1, key: 'retro', label: 'Retro', bg: '#9bbc0f', fg: '#0f380f' },
  { id: 2, key: 'den', label: 'Den', bg: '#ffffff', fg: '#e65f00' },
];

let root;

const sw = (id, on) => `<label class="switch"><input type="checkbox" id="${id}" ${on ? 'checked' : ''}><span></span></label>`;

export function initSettings(el) {
  root = el;
  bus.on('status', () => { if (!root.contains(document.activeElement)) render(); });
  bus.on('connection', render);
  render();
}

function render() {
  const s = state.status || {};
  const online = state.conn === 'connected';
  const app = loadSettings();
  const f = s.f || [0, 2, 3, 8];

  root.innerHTML = `<div class="wrap">
  <div class="panel ${online ? '' : 'disabled-block'}">
    <h3>CykloComp ${online ? `· fw ${s.fw || '?'} · ${s.brd === 1 ? 'PCB' : 'breadboard'}` : '(nepřipojen)'}</h3>
    <div class="set-row" style="display:block">
      <label>Barevné téma displeje</label>
      <div class="theme-pick" style="margin-top:8px">
        ${THEMES.map((t) => `<button data-thm="${t.id}" class="${s.thm === t.id ? 'on' : ''}" style="background:${t.bg};color:${t.fg}">${t.label}</button>`).join('')}
      </div>
    </div>
    <div class="set-row"><div><label>Jas podsvícení</label><div class="desc">jen displej s pinem BLK (PCB)</div></div>
      <input type="range" id="sBl" min="5" max="100" step="5" value="${s.bl ?? 80}"></div>
    <div class="set-row"><div><label>Autopauza</label><div class="desc">čas stojí, když stojíš</div></div>${sw('sAp', s.ap !== 0)}</div>
    <div class="set-row"><div><label>BLE senzory</label><div class="desc">hrudní pás (tep) a senzor kadence</div></div>${sw('sSen', s.sen === 1)}</div>
    <div class="set-row"><div><label>Automatické vypnutí</label><div class="desc">když se nejede a nic se neděje</div></div>
      <select id="sOff">${[0, 5, 10, 15, 30, 60].map((m) => `<option value="${m}" ${s.aoff === m ? 'selected' : ''}>${m ? `${m} min` : 'nikdy'}</option>`).join('')}</select></div>
    <div class="set-row"><div><label>Rozsah budíku</label><div class="desc">maximum oblouku rychlosti</div></div>
      <select id="sGmax">${[40, 50, 60, 70, 80].map((v) => `<option value="${v}" ${s.gmax === v ? 'selected' : ''}>${v} km/h</option>`).join('')}</select></div>
    <div class="set-row" style="display:block"><label>Dlaždice na displeji (2×2)</label>
      <div class="fields-grid" style="margin-top:8px">
        ${[0, 1, 2, 3].map((slot) => `<select data-slot="${slot}">${FIELD_OPTIONS.map((o, i) => `<option value="${i}" ${f[slot] === i ? 'selected' : ''}>${o}</option>`).join('')}</select>`).join('')}
      </div></div>
    <div class="set-row"><div><label>Odometr</label><div class="desc">celkem najeto (km)</div></div>
      <div style="display:flex;gap:6px"><input type="number" id="sOdo" value="${s.odo ?? ''}" step="0.1" style="width:110px"><button class="chip" id="sOdoSave">Uložit</button></div></div>
    <div class="set-row"><div><label>Simulace GPS</label><div class="desc">testovací jízda bez signálu</div></div>${sw('sSim', s.sim === 1)}</div>
    <div class="set-row"><div><label>Nulovat trip</label></div><button class="chip" id="sReset">Nulovat</button></div>
    <div class="row" style="margin-top:12px">
      <button class="btn ghost small" id="sDisc">Odpojit</button>
      <button class="btn danger small" id="sOffNow">⏻ Vypnout CykloComp</button>
    </div>
  </div>

  <div class="panel">
    <h3>Aplikace</h3>
    <div class="set-row"><div><label>Vzhled aplikace</label></div>
      <select id="aTheme">
        ${[['device', 'Jako CykloComp'], ['noc', 'Noc'], ['retro', 'Retro (Game Boy)'], ['den', 'Den']].map(([v, l]) => `<option value="${v}" ${app.appTheme === v ? 'selected' : ''}>${l}</option>`).join('')}
      </select></div>
    <div class="set-row"><div><label>Mazat jízdy z CykloCompu</label><div class="desc">po úspěšném stažení do telefonu</div></div>${sw('aDel', app.deleteAfterSync)}</div>
    <div class="set-row"><div><label>Záloha dat</label><div class="desc">jízdy + profil do souboru</div></div>
      <div style="display:flex;gap:6px"><button class="chip" id="aExport">Export</button><button class="chip" id="aImport">Import</button></div></div>
    <div class="set-row"><div><label>Smazat vše</label></div><button class="chip" id="aWipe">Smazat</button></div>
    <input type="file" id="aImportFile" accept="application/json" class="hidden">
  </div>
  <p class="hint">CykloComp web app · data zůstávají jen v tomto telefonu.<br>Mapy © OpenStreetMap, trasy BRouter / OSRM.</p>
  </div>`;

  const cmd = (c) => send(c).then(() => setTimeout(() => send('INFO').catch(() => {}), 150)).catch(() => {});
  $$('[data-thm]', root).forEach((b) => { b.onclick = () => { cmd(`THEME:${b.dataset.thm}`); if (state.status) state.status.thm = +b.dataset.thm; applyTheme(); render(); }; });
  $('#sBl', root).onchange = (e) => cmd(`BL:${e.target.value}`);
  $('#sAp', root).onchange = (e) => cmd(`AP:${e.target.checked ? 1 : 0}`);
  $('#sSen', root).onchange = (e) => cmd(`SENS:${e.target.checked ? 1 : 0}`);
  $('#sOff', root).onchange = (e) => cmd(`AUTOOFF:${e.target.value}`);
  $('#sGmax', root).onchange = (e) => cmd(`GAUGE:${e.target.value}`);
  $('#sSim', root).onchange = (e) => cmd(`SIM:${e.target.checked ? 1 : 0}`);
  $$('[data-slot]', root).forEach((sel) => {
    sel.onchange = () => {
      const vals = $$('[data-slot]', root).map((x) => x.value);
      cmd(`CFG:FIELDS:${vals.join(',')}`);
    };
  });
  $('#sOdoSave', root).onclick = () => { const v = parseFloat($('#sOdo', root).value); if (Number.isFinite(v)) { cmd(`ODO:${v}`); toast('Odometr uložen'); } };
  $('#sReset', root).onclick = () => { if (confirm('Vynulovat trip (vzdálenost, čas, max)?')) cmd('RESET_TRIP'); };
  $('#sDisc', root).onclick = () => disconnect();
  $('#sOffNow', root).onclick = () => { if (confirm('Vypnout CykloComp? Zapneš ho tlačítkem MODE.')) send('PWROFF').catch(() => {}); };

  $('#aTheme', root).onchange = (e) => { saveSettings({ ...loadSettings(), appTheme: e.target.value }); applyTheme(); };
  $('#aDel', root).onchange = (e) => saveSettings({ ...loadSettings(), deleteAfterSync: e.target.checked });
  $('#aExport', root).onclick = async () => {
    const data = { app: 'cyklocomp', version: 1, exported: new Date().toISOString(), profile: loadProfile(), rides: await loadRides() };
    download(`cyklocomp-zaloha-${new Date().toISOString().slice(0, 10)}.json`, JSON.stringify(data), 'application/json');
  };
  $('#aImport', root).onclick = () => $('#aImportFile', root).click();
  $('#aImportFile', root).onchange = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const data = JSON.parse(await file.text());
      for (const r of data.rides || []) await saveRide(r);
      if (data.profile) saveProfile(data.profile);
      bus.emit('rides-changed');
      toast(`Obnoveno ${(data.rides || []).length} jízd`);
    } catch { toast('Soubor zálohy nejde načíst'); }
  };
  $('#aWipe', root).onclick = async () => {
    if (!confirm('Smazat všechny jízdy a profil v tomto telefonu?')) return;
    await clearRides();
    localStorage.removeItem('cc.profile');
    localStorage.removeItem('cc.synced');
    bus.emit('rides-changed');
    toast('Smazáno');
  };
}
