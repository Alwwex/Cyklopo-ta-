// Tema aplikace: bud stejne jako displej CykloCompu, nebo vlastni volba.
import { bus } from './util.js';
import { loadSettings } from './storage.js';
import { state } from './device.js';

const KEYS = ['noc', 'retro', 'den'];
const META = { noc: '#04080f', retro: '#9bbc0f', den: '#f4f6f9' };

export function applyTheme() {
  const pref = loadSettings().appTheme;
  const key = pref === 'device' ? (KEYS[state.status?.thm ?? 0] || 'noc') : pref;
  if (document.documentElement.dataset.theme !== key) {
    document.documentElement.dataset.theme = key;
    document.querySelector('meta[name="theme-color"]').setAttribute('content', META[key]);
    bus.emit('theme', key);
  }
}

bus.on('status', applyTheme);
