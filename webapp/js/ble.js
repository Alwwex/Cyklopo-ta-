// Web Bluetooth spojeni s CykloComp (Chrome / Edge na Androidu a na PC).
// Protokol je stejny jako pro React Native appku - viz README v koreni repa.
import { bus, sleep, withTimeout } from './util.js';

export const SERVICE_UUID = 'a5c40001-2f0b-4f6e-9d3a-8c1e2b7d9f10';
const TELEMETRY_UUID = 'a5c40002-2f0b-4f6e-9d3a-8c1e2b7d9f10';
const COMMAND_UUID = 'a5c40003-2f0b-4f6e-9d3a-8c1e2b7d9f10';
const STATUS_UUID = 'a5c40004-2f0b-4f6e-9d3a-8c1e2b7d9f10';
const BULK_UUID = 'a5c40005-2f0b-4f6e-9d3a-8c1e2b7d9f10';

// prikazy drzime pod 180 B - projdou i pri malem MTU (Android pak udela "long write")
const MAX_CMD_LEN = 180;

const enc = new TextEncoder();
const dec = new TextDecoder();

// GATT operace musi jit po jedne, jinak Chrome hazi "GATT operation already in progress"
class GattQueue {
  constructor() { this.tail = Promise.resolve(); }
  run(fn) {
    const p = this.tail.then(fn, fn);
    this.tail = p.catch(() => {});
    return p;
  }
}

export class BleDevice {
  constructor() {
    this.kind = 'ble';
    this.device = null;
    this.ch = {};
    this.q = new GattQueue();
    this.connected = false;
    this.wantConnected = false;
    this.reading = { tel: false, sta: false };
    this._onTel = (e) => this._handle('tel', e.target.value);
    this._onSta = (e) => this._handle('sta', e.target.value);
    this._onGone = () => this._disconnected();
  }

  static supported() {
    return typeof navigator !== 'undefined' && !!navigator.bluetooth;
  }

  get name() { return this.device?.name || 'CykloComp'; }

  // vyber zarizeni v systemovem dialogu (musi byt vyvolano klepnutim)
  async pick() {
    const dev = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }, { name: 'CykloComp' }],
      optionalServices: [SERVICE_UUID],
    });
    this._setDevice(dev);
    await this.connect();
  }

  // pokus o pripojeni k uz jednou povolenemu zarizeni bez dialogu
  async tryRemembered() {
    if (!navigator.bluetooth?.getDevices) return false;
    const list = await navigator.bluetooth.getDevices();
    const id = localStorage.getItem('cc.lastDeviceId');
    const dev = list.find((d) => d.id === id) || list.find((d) => d.name === 'CykloComp');
    if (!dev) return false;
    this._setDevice(dev);
    try { await this.connect(); return true; } catch { return false; }
  }

  _setDevice(dev) {
    if (this.device) this.device.removeEventListener('gattserverdisconnected', this._onGone);
    this.device = dev;
    localStorage.setItem('cc.lastDeviceId', dev.id);
    dev.addEventListener('gattserverdisconnected', this._onGone);
  }

  async connect() {
    this.wantConnected = true;
    bus.emit('connection', 'connecting');
    try {
      const server = await withTimeout(this.device.gatt.connect(), 15000, 'CykloComp neodpovida');
      const svc = await server.getPrimaryService(SERVICE_UUID);
      this.ch.tel = await svc.getCharacteristic(TELEMETRY_UUID);
      this.ch.cmd = await svc.getCharacteristic(COMMAND_UUID);
      this.ch.sta = await svc.getCharacteristic(STATUS_UUID).catch(() => null);  // stary firmware je nema
      this.ch.blk = await svc.getCharacteristic(BULK_UUID).catch(() => null);

      this.ch.tel.removeEventListener('characteristicvaluechanged', this._onTel);
      this.ch.tel.addEventListener('characteristicvaluechanged', this._onTel);
      await this.q.run(() => this.ch.tel.startNotifications());
      if (this.ch.sta) {
        this.ch.sta.removeEventListener('characteristicvaluechanged', this._onSta);
        this.ch.sta.addEventListener('characteristicvaluechanged', this._onSta);
        await this.q.run(() => this.ch.sta.startNotifications());
      }
      this.connected = true;
      bus.emit('connection', 'connected');
      this._read('tel');
      if (this.ch.sta) this._read('sta');
    } catch (e) {
      this.connected = false;
      bus.emit('connection', 'disconnected');
      throw e;
    }
  }

  async disconnect() {
    this.wantConnected = false;
    if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    this.connected = false;
    bus.emit('connection', 'disconnected');
  }

  async _disconnected() {
    this.connected = false;
    bus.emit('connection', this.wantConnected ? 'connecting' : 'disconnected');
    // automaticke znovupripojeni (napr. telefon v kapse, chvilkovy vypadek)
    let delay = 1000;
    while (this.wantConnected && !this.connected) {
      await sleep(delay);
      if (!this.wantConnected) break;
      try { await this.connect(); } catch { delay = Math.min(delay * 2, 30000); }
    }
  }

  // Notifikace muze byt pri malem MTU useknuta -> dočteme celou hodnotu (Read Blob)
  _handle(kind, dataView) {
    const text = dec.decode(dataView);
    try {
      this._emit(kind, JSON.parse(text));
    } catch {
      this._read(kind);
    }
  }

  async _read(kind) {
    const c = this.ch[kind];
    if (!c || this.reading[kind]) return;
    this.reading[kind] = true;
    try {
      const v = await this.q.run(() => c.readValue());
      this._emit(kind, JSON.parse(dec.decode(v)));
    } catch (e) {
      console.warn('cteni', kind, e);
    } finally {
      this.reading[kind] = false;
    }
  }

  _emit(kind, obj) {
    bus.emit(kind === 'tel' ? 'telemetry' : 'status', obj);
  }

  async send(cmd) {
    if (!this.connected) throw new Error('CykloComp neni pripojen');
    const data = enc.encode(cmd);
    const c = this.ch.cmd;
    return this.q.run(() => (c.writeValueWithResponse ? c.writeValueWithResponse(data) : c.writeValue(data)));
  }

  // dotaz pres "bulk" charakteristiku: posli SYNC:xxx a cti, dokud neprijde odpoved na nas dotaz
  async bulk(arg) {
    if (!this.ch.blk) throw new Error('Firmware neumi synchronizaci (aktualizuj na v2)');
    const tag = arg.split(':').slice(0, 3).join(':');
    await this.send(`SYNC:${arg}`);
    for (let i = 0; i < 40; i++) {
      const v = await this.q.run(() => this.ch.blk.readValue());
      try {
        const j = JSON.parse(dec.decode(v));
        if (j.req === tag) return j;
      } catch { /* jeste se nezapsalo */ }
      await sleep(60);
    }
    throw new Error(`Bez odpovedi na SYNC:${arg}`);
  }

  async sendPoints(points, prefix, beginCmd, endCmd, onProgress) {
    await this.send(beginCmd);
    let buf = '';
    let sent = 0;
    const flush = async () => {
      if (!buf) return;
      await this.send(prefix + buf);
      buf = '';
      onProgress && onProgress(sent / points.length);
    };
    for (const p of points) {
      const entry = `${p.lat.toFixed(5)},${p.lng.toFixed(5)};`;
      if (prefix.length + buf.length + entry.length > MAX_CMD_LEN) await flush();
      buf += entry;
      sent++;
    }
    await flush();
    await this.send(endCmd);
    onProgress && onProgress(1);
  }
}

// Stazeni ulozenych jizd z CykloCompu (firmware v2+).
export async function downloadRides(dev, { onProgress, skipIds = new Set() } = {}) {
  const list = await dev.bulk('LIST');
  const ids = (list.ids || []).filter((id) => !skipIds.has(id));
  const rides = [];
  for (let k = 0; k < ids.length; k++) {
    const id = ids[k];
    const sum = await dev.bulk(`SUM:${id}`);
    if (sum.err) continue;
    const track = [];
    let pg = 0;
    for (;;) {
      const r = await dev.bulk(`TRK:${id}:${pg}`);
      if (r.err) break;
      for (const pair of (r.p || '').split(';')) {
        if (!pair) continue;
        const [lat, lng] = pair.split(',').map(Number);
        if (Number.isFinite(lat) && Number.isFinite(lng)) track.push([lat, lng]);
      }
      onProgress && onProgress((k + Math.min(1, ((pg + 1) * 24) / Math.max(1, r.n))) / ids.length);
      if (!r.more) break;
      pg++;
    }
    rides.push({ deviceRideId: id, summary: sum, track });
  }
  return rides;
}
