// Spravce pripojeni + sdileny stav appky + synchronizace jizd z CykloCompu.
import { bus, toast, openModal, escapeHtml } from './util.js';
import { BleDevice, downloadRides } from './ble.js';
import { DemoDevice } from './demo.js';
import {
  loadRides, saveRide, loadProfile, saveProfile, loadSettings, loadSyncedKeys, saveSyncedKeys,
} from './storage.js';
import { processFinishedRide, BADGES } from './gamification.js';

export const state = {
  dev: null,
  conn: 'disconnected',
  tel: null,
  status: null,
  ridden: [],          // stopa aktualni jizdy (pro mapu v appce)
  syncing: false,
};

let prevRid = null;
let localRide = null;  // zalozni zaznam jizdy pro stary firmware bez SYNC

bus.on('connection', (c) => {
  state.conn = c;
  if (c === 'connected') onConnected();
  if (c === 'disconnected') { state.tel = null; prevRid = null; }
});

bus.on('status', (s) => { state.status = s; });

bus.on('telemetry', (t) => {
  state.tel = t;
  if (t.rid === 1) {
    if (prevRid === 0) { state.ridden = []; localRide = { start: new Date(), max: 0, track: [] }; }
    if (t.fix === 1) {
      const last = state.ridden[state.ridden.length - 1];
      if (!last || Math.abs(last[0] - t.lat) + Math.abs(last[1] - t.lng) > 0.00008) state.ridden.push([t.lat, t.lng]);
    }
    if (localRide) { localRide.max = Math.max(localRide.max, t.spd); localRide.last = t; }
  } else if (prevRid === 1) {
    onRideEnded();
  }
  prevRid = t.rid;
});

export async function connect(kind) {
  if (state.dev) await state.dev.disconnect().catch(() => {});
  state.dev = kind === 'demo' ? new DemoDevice() : new BleDevice();
  try {
    await state.dev.pick();
  } catch (e) {
    state.dev = null;
    bus.emit('connection', 'disconnected');
    if (e && e.name === 'NotFoundError') return; // uzivatel zavrel dialog
    toast(`Připojení selhalo: ${e.message || e}`, 4000);
  }
}

export async function tryAutoConnect() {
  if (!BleDevice.supported()) return;
  const dev = new BleDevice();
  state.dev = dev;
  const ok = await dev.tryRemembered().catch(() => false);
  if (!ok && state.dev === dev && state.conn !== 'connected') state.dev = null;
}

export async function disconnect() {
  if (state.dev) await state.dev.disconnect();
  state.dev = null;
}

export async function send(cmd) {
  if (!state.dev || state.conn !== 'connected') { toast('CykloComp není připojen'); throw new Error('offline'); }
  return state.dev.send(cmd);
}

async function onConnected() {
  toast(`Připojeno: ${state.dev?.name || 'CykloComp'}`);
  try {
    // hodiny a casove pasmo z telefonu - CykloComp ukazuje cas i bez GPS
    await state.dev.send(`TIME:${Math.floor(Date.now() / 1000)}`);
    await state.dev.send(`SET_TZ:${Math.round(-new Date().getTimezoneOffset() / 60)}`);
    await state.dev.send('INFO');
  } catch (e) { console.warn(e); }
  setTimeout(() => syncRides({ quiet: true }), 1500);
}

async function onRideEnded() {
  // CykloComp v2 si jizdu ulozi sam -> stahneme ji. Starsi firmware: ulozime z telemetrie.
  await new Promise((r) => setTimeout(r, 1500));
  const fs = state.status ? state.status.fs : 0;
  if (fs && state.dev?.bulk) {
    syncRides({ quiet: false });
  } else if (localRide?.last && localRide.last.dst > 0.05) {
    const t = localRide.last;
    await storeRide({
      id: `app-${localRide.start.getTime()}`,
      source: 'app',
      dateISO: localRide.start.toISOString(),
      distanceKm: t.dst, timeS: t.tim, avgKmh: t.tim ? t.dst / (t.tim / 3600) : 0,
      maxKmh: localRide.max, ascentM: t.asc || 0, track: state.ridden.slice(),
    }, true);
  }
  localRide = null;
}

async function storeRide(ride, showReward) {
  const rides = await loadRides();
  const profile = await loadProfile();
  const res = processFinishedRide(profile, rides, ride);
  await saveRide({ ...ride, points: res.pointsEarned });
  saveProfile(res.profile);
  bus.emit('rides-changed');
  if (showReward) showRewardModal(ride, res);
  return res;
}

export async function syncRides({ quiet = false } = {}) {
  if (!state.dev || state.conn !== 'connected' || state.syncing) return;
  if (state.status && state.status.fs === 0) { if (!quiet) toast('CykloComp nemá úložiště (Partition Scheme!)'); return; }
  state.syncing = true;
  bus.emit('sync', { active: true, progress: 0 });
  const synced = loadSyncedKeys();
  const settings = loadSettings();
  let count = 0;
  let last = null;
  try {
    const rides = await downloadRides(state.dev, { onProgress: (p) => bus.emit('sync', { active: true, progress: p }) });
    for (const r of rides) {
      const s = r.summary;
      const key = `${s.id}-${s.ts}-${Math.round(s.dst * 1000)}`;
      if (!synced.has(key)) {
        const date = s.ts ? new Date(s.ts * 1000) : new Date();
        const ride = {
          id: `cc-${key}`, source: 'device', deviceRideId: s.id, dateISO: date.toISOString(),
          distanceKm: s.dst, timeS: s.tim, avgKmh: s.avg, maxKmh: s.max, ascentM: s.asc, track: r.track,
        };
        last = { ride, res: await storeRide(ride, false) };
        synced.add(key);
        count++;
      }
      if (settings.deleteAfterSync) await state.dev.bulk(`DEL:${s.id}`).catch(() => {});
    }
    saveSyncedKeys(synced);
    if (count && last) showRewardModal(last.ride, last.res, count);
    else if (!quiet) toast('Žádné nové jízdy');
  } catch (e) {
    console.warn(e);
    if (!quiet) toast(`Synchronizace selhala: ${e.message || e}`, 4000);
  } finally {
    state.syncing = false;
    bus.emit('sync', { active: false, progress: 1 });
  }
}

function showRewardModal(ride, res, count = 1) {
  const badges = res.newBadges.map((b) => `<div class="badge-card"><span class="ic">${b.icon}</span>${escapeHtml(b.label)}</div>`).join('');
  openModal(`
    <div class="reward">
      <div class="muted small">${count > 1 ? `Staženo ${count} jízd · poslední:` : 'Jízda uložena'}</div>
      <h2>${ride.distanceKm.toFixed(2)} km</h2>
      <div class="pts">+${res.pointsEarned} b.</div>
      ${res.profile.streak > 1 ? `<p>🔥 Streak ${res.profile.streak} dní v řadě!</p>` : ''}
      ${badges ? `<h3 class="muted small">NOVÉ ODZNAKY</h3><div class="badges" style="margin-bottom:14px">${badges}</div>` : ''}
      <button class="btn" data-close>Super!</button>
    </div>`);
}

export { BADGES };
