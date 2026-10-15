// Demo zarizeni - chova se jako CykloComp (telemetrie, prikazy, SYNC),
// takze appku jde vyzkouset bez hardwaru, na PC i na iPhonu.
import { bus, haversineM, sleep } from './util.js';

const START = { lat: 50.0755, lng: 14.4378 };

function bearing(a, b) {
  const toRad = (x) => (x * Math.PI) / 180;
  const y = Math.sin(toRad(b.lng - a.lng)) * Math.cos(toRad(b.lat));
  const x = Math.cos(toRad(a.lat)) * Math.sin(toRad(b.lat)) -
    Math.sin(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.cos(toRad(b.lng - a.lng));
  return (Math.atan2(y, x) * 180) / Math.PI;
}

function demoRideTrack() {
  const pts = [];
  for (let i = 0; i <= 160; i++) {
    const t = (i / 160) * 2 * Math.PI;
    pts.push([
      +(START.lat + 0.02 * Math.sin(t) + 0.003 * Math.sin(4 * t)).toFixed(5),
      +(START.lng + 0.035 * (1 - Math.cos(t)) + 0.004 * Math.cos(3 * t)).toFixed(5),
    ]);
  }
  return pts;
}

export class DemoDevice {
  constructor() {
    this.kind = 'demo';
    this.name = 'CykloComp (demo)';
    this.connected = false;
    this.timer = null;
    this.pos = { ...START };
    this.heading = 40;
    this.t = 0;
    this.route = [];
    this.routeIdx = 0;
    this.rx = { prefix: null, buf: [] };
    this.s = {
      rid: 0, pau: 0, dst: 0, tim: 0, max: 0, asc: 0, alt: 300, odo: 1234.5,
      thm: 0, bl: 80, ap: 1, sen: 1, aoff: 15, gmax: 60, sim: 1, f: [0, 2, 3, 8], tz: 2,
    };
    const track = demoRideTrack();
    this.stored = new Map([[7, {
      summary: { id: 7, ts: Math.floor(Date.now() / 1000) - 86400 - 7200, dst: 28.412, tim: 4520, max: 47.3, avg: 22.6, asc: 412, pts: track.length, done: 1 },
      track,
    }]]);
    this.nextId = 8;
  }

  static supported() { return true; }

  async pick() { await this.connect(); }
  async tryRemembered() { return false; }

  async connect() {
    bus.emit('connection', 'connecting');
    await sleep(500);
    this.connected = true;
    bus.emit('connection', 'connected');
    this.timer = setInterval(() => this._tick(), 1000);
    this._tick();
    this._status();
  }

  async disconnect() {
    clearInterval(this.timer);
    this.connected = false;
    bus.emit('connection', 'disconnected');
  }

  _tick() {
    const s = this.s;
    this.t += 1;
    const stopped = this.t % 90 > 84;
    const kmh = stopped ? 0 : 24 + 8 * Math.sin(this.t / 23) + 3 * Math.sin(this.t / 5);
    const step = kmh / 3.6;
    if (this.route.length > 1) {
      const target = this.route[this.routeIdx % this.route.length];
      if (haversineM(this.pos, target) < step + 1) this.routeIdx = (this.routeIdx + 1) % this.route.length;
      this.heading = bearing(this.pos, target);
    } else {
      this.heading += 5 * Math.sin(this.t / 40);
    }
    const h = (this.heading * Math.PI) / 180;
    this.pos.lat += (step * Math.cos(h)) / 111320;
    this.pos.lng += (step * Math.sin(h)) / (111320 * Math.cos((this.pos.lat * Math.PI) / 180));
    const alt = 300 + 45 * Math.sin(this.t / 90) + 8 * Math.sin(this.t / 17);
    if (alt - s.alt > 0 && kmh > 0) s.asc += alt - s.alt;
    s.alt = alt;
    const moving = kmh > 2.5;
    if (s.rid && !s.pau && moving) {
      s.dst += step / 1000;
      s.tim += 1;
      s.max = Math.max(s.max, kmh);
      s.odo += step / 1000;
      if (this.t % 4 === 0) (this.ridden ||= []).push([+this.pos.lat.toFixed(5), +this.pos.lng.toFixed(5)]);
    }
    bus.emit('telemetry', {
      fix: 1, spd: +kmh.toFixed(1), lat: +this.pos.lat.toFixed(6), lng: +this.pos.lng.toFixed(6),
      alt: +alt.toFixed(1), dst: +s.dst.toFixed(3), tim: s.tim, sat: 9, rid: s.rid, pau: s.pau,
      bat: 78, asc: Math.round(s.asc), max: +s.max.toFixed(1),
      hr: s.sen ? Math.round(128 + 12 * Math.sin(this.t / 30)) : 0,
      cad: s.sen && moving ? Math.round(84 + 5 * Math.sin(this.t / 9)) : 0,
    });
    if (this.t % 5 === 0) this._status(!moving && s.rid && !s.pau);
  }

  _status(apa = false) {
    const s = this.s;
    bus.emit('status', {
      fw: '2.0.0-demo', brd: 1, odo: +s.odo.toFixed(1), vb: 3960, chg: 0, thm: s.thm, bl: s.bl, ap: s.ap,
      sen: s.sen, hrc: s.sen, csc: s.sen, grd: +(4 * Math.sin(this.t / 60)).toFixed(1),
      avg: s.tim ? +((s.dst / s.tim) * 3600).toFixed(1) : 0, hdp: 0.9, crs: Math.round(this.heading),
      scr: 0, tz: s.tz, aoff: s.aoff, gmax: s.gmax, sim: s.sim, apa: apa ? 1 : 0, fs: 1, rcur: s.rid ? this.nextId : 0, f: s.f,
    });
  }

  async send(cmd) {
    if (!this.connected) throw new Error('Demo neni pripojeno');
    await sleep(8);
    const s = this.s;
    const [head, ...rest] = cmd.split(':');
    const arg = rest.join(':');
    if (cmd === 'RIDE:START' && !s.rid) { Object.assign(s, { rid: 1, pau: 0, dst: 0, tim: 0, max: 0, asc: 0 }); this.ridden = []; }
    else if (cmd === 'RIDE:STOP' && s.rid) {
      if (s.dst > 0.05) {
        const id = this.nextId++;
        this.stored.set(id, {
          summary: { id, ts: Math.floor(Date.now() / 1000) - s.tim, dst: s.dst, tim: s.tim, max: s.max, avg: s.tim ? (s.dst / s.tim) * 3600 : 0, asc: Math.round(s.asc), pts: (this.ridden || []).length, done: 1 },
          track: this.ridden || [],
        });
      }
      s.rid = 0; s.pau = 0;
    } else if (cmd === 'RIDE:PAUSE') s.pau = 1;
    else if (cmd === 'RIDE:RESUME') s.pau = 0;
    else if (cmd === 'RESET_TRIP') Object.assign(s, { dst: 0, tim: 0, max: 0, asc: 0 });
    else if (head === 'THEME') s.thm = +arg;
    else if (head === 'BL') s.bl = +arg;
    else if (head === 'AP') s.ap = +arg;
    else if (head === 'SENS') s.sen = +arg;
    else if (head === 'AUTOOFF') s.aoff = +arg;
    else if (head === 'GAUGE') s.gmax = +arg;
    else if (head === 'SIM') s.sim = +arg;
    else if (head === 'SET_TZ') s.tz = +arg;
    else if (head === 'ODO') s.odo = +arg;
    else if (cmd.startsWith('CFG:FIELDS:')) s.f = cmd.slice(11).split(',').map(Number);
    else if (cmd === 'PWROFF') { this.disconnect(); return; }
    else if (cmd === 'RT:BEGIN' || cmd === 'MP:BEGIN') this.rx = { prefix: head, buf: [] };
    else if (cmd.startsWith('RT:P:') || cmd.startsWith('MP:P:')) {
      for (const pair of cmd.slice(5).split(';')) {
        if (!pair) continue;
        const [lat, lng] = pair.split(',').map(Number);
        this.rx.buf.push({ lat, lng });
      }
    } else if (cmd === 'RT:END') { this.route = this.rx.buf; this.routeIdx = 0; this.pos = { ...this.route[0] }; }
    else if (cmd === 'RT:CLEAR') this.route = [];
    else if (head === 'SYNC') this._sync(arg);
    if (['THEME', 'BL', 'AP', 'SENS', 'AUTOOFF', 'GAUGE', 'SIM', 'CFG', 'ODO', 'RIDE'].includes(head)) this._status();
  }

  _sync(arg) {
    const [what, a, b] = arg.split(':');
    if (what === 'LIST') this.bulkValue = { req: 'LIST', cur: 0, ids: [...this.stored.keys()] };
    else if (what === 'SUM') this.bulkValue = { req: `SUM:${a}`, ...this.stored.get(+a)?.summary };
    else if (what === 'TRK') {
      const tr = this.stored.get(+a)?.track || [];
      const pg = +b;
      const part = tr.slice(pg * 24, pg * 24 + 24);
      this.bulkValue = { req: `TRK:${a}:${b}`, n: tr.length, more: (pg + 1) * 24 < tr.length ? 1 : 0, p: part.map(([la, lo]) => `${la},${lo};`).join('') };
    } else if (what === 'DEL') { this.stored.delete(+a); this.bulkValue = { req: `DEL:${a}`, ok: 1 }; }
  }

  async bulk(arg) {
    await this.send(`SYNC:${arg}`);
    return this.bulkValue;
  }

  async sendPoints(points, prefix, beginCmd, endCmd, onProgress) {
    await this.send(beginCmd);
    for (let i = 0; i < points.length; i += 9) {
      const chunk = points.slice(i, i + 9).map((p) => `${p.lat.toFixed(5)},${p.lng.toFixed(5)};`).join('');
      await this.send(prefix + chunk);
      onProgress && onProgress(Math.min(1, (i + 9) / points.length));
    }
    await this.send(endCmd);
  }
}
