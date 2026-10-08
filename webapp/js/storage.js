// Ulozeni dat v prohlizeci: jizdy v IndexedDB (stopy jsou velke),
// profil / nastaveni / planovana trasa v localStorage.
import { defaultProfile } from './gamification.js';

const DB_NAME = 'cyklocomp';
const STORE = 'rides';
let dbPromise = null;

function db() {
  if (!dbPromise) {
    dbPromise = new Promise((resolve, reject) => {
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => {
        const d = req.result;
        if (!d.objectStoreNames.contains(STORE)) d.createObjectStore(STORE, { keyPath: 'id' });
      };
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  }
  return dbPromise;
}

async function tx(mode, fn) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const t = d.transaction(STORE, mode);
    const store = t.objectStore(STORE);
    const res = fn(store);
    t.oncomplete = () => resolve(res && 'result' in res ? res.result : undefined);
    t.onerror = () => reject(t.error);
  });
}

export async function loadRides() {
  const all = (await tx('readonly', (s) => s.getAll())) || [];
  return all.sort((a, b) => (b.dateISO || '').localeCompare(a.dateISO || ''));
}
export const saveRide = (ride) => tx('readwrite', (s) => s.put(ride));
export const deleteRide = (id) => tx('readwrite', (s) => s.delete(id));
export const clearRides = () => tx('readwrite', (s) => s.clear());

function getJSON(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch { return fallback; }
}
const setJSON = (key, v) => localStorage.setItem(key, JSON.stringify(v));

export const loadProfile = () => ({ ...defaultProfile(), ...getJSON('cc.profile', {}) });
export const saveProfile = (p) => setJSON('cc.profile', p);

export const loadSettings = () => ({
  appTheme: 'device',        // 'device' = stejne jako CykloComp, jinak noc/retro/den
  mapLayer: 'cyclosm',
  routeProfile: 'trekking',
  deleteAfterSync: true,
  wakeLock: false,
  ...getJSON('cc.settings', {}),
});
export const saveSettings = (s) => setJSON('cc.settings', s);

export const loadPlan = () => getJSON('cc.plan', { waypoints: [], route: [], distanceM: 0, ascentM: 0, elevations: [] });
export const savePlan = (p) => setJSON('cc.plan', p);

export const loadSyncedKeys = () => new Set(getJSON('cc.synced', []));
export const saveSyncedKeys = (set) => setJSON('cc.synced', [...set].slice(-500));
