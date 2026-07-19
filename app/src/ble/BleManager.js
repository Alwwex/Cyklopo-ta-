import { BleManager as PlxManager } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import { SERVICE_UUID, TELEMETRY_UUID, COMMAND_UUID, DEVICE_NAME, REQUESTED_MTU } from './uuids';
import { requestBlePermissions } from './permissions';

// jednoducha emitter implementace, aby appka nezavisela na 'events' modulu
class Emitter {
  constructor() { this.listeners = {}; }
  on(event, cb) {
    if (!this.listeners[event]) this.listeners[event] = [];
    this.listeners[event].push(cb);
    return () => { this.listeners[event] = this.listeners[event].filter((l) => l !== cb); };
  }
  emit(event, payload) {
    (this.listeners[event] || []).forEach((cb) => cb(payload));
  }
}

const MAX_MTU_HEADROOM = 3; // ATT hlavicka pro write-without-response

class CykloBleManager {
  constructor() {
    this.plx = new PlxManager();
    this.emitter = new Emitter();
    this.device = null;
    this.telemetrySub = null;
    this.connected = false;
    this.mapReceiving = false;
  }

  onTelemetry(cb) { return this.emitter.on('telemetry', cb); }
  onConnectionChange(cb) { return this.emitter.on('connection', cb); }
  onError(cb) { return this.emitter.on('error', cb); }

  isConnected() { return this.connected; }

  setConnected(value) {
    this.connected = value;
    this.emitter.emit('connection', value);
  }

  // Po reloadu appky Android drzi stare BLE spojeni v systemu, i kdyz nas JS
  // stav o nem nevi - ESP pak neinzeruje a sken nic nenajde. Pred skenovanim
  // vzdy zkontrolujeme, jestli uz nejake spojene zarizeni s nasi sluzbou neexistuje.
  async adoptExistingConnection() {
    try {
      const devices = await this.plx.connectedDevices([SERVICE_UUID]);
      if (devices && devices.length > 0) {
        await this._bindDevice(devices[0]);
        return true;
      }
    } catch (e) {
      this.emitter.emit('error', { stage: 'adopt', error: e });
    }
    return false;
  }

  async scanAndConnect({ timeoutMs = 10000 } = {}) {
    const granted = await requestBlePermissions();
    if (!granted) throw new Error('BLE opravneni nebylo udeleno');

    const adopted = await this.adoptExistingConnection();
    if (adopted) return this.device;

    return new Promise((resolve, reject) => {
      let finished = false;
      const timer = setTimeout(() => {
        if (finished) return;
        finished = true;
        this.plx.stopDeviceScan();
        reject(new Error('CykloComp nenalezen'));
      }, timeoutMs);

      this.plx.startDeviceScan([SERVICE_UUID], null, async (error, scannedDevice) => {
        if (error) {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          this.plx.stopDeviceScan();
          reject(error);
          return;
        }
        if (!scannedDevice) return;
        if (scannedDevice.name !== DEVICE_NAME && !(scannedDevice.localName === DEVICE_NAME)) return;
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        this.plx.stopDeviceScan();
        try {
          const connected = await scannedDevice.connect({ requestMTU: REQUESTED_MTU });
          await this._bindDevice(connected);
          resolve(this.device);
        } catch (e) {
          reject(e);
        }
      });
    });
  }

  async _bindDevice(device) {
    const withServices = await device.discoverAllServicesAndCharacteristics();
    this.device = withServices;
    this.setConnected(true);

    device.onDisconnected(() => {
      this.setConnected(false);
      this.device = null;
      if (this.telemetrySub) { this.telemetrySub.remove(); this.telemetrySub = null; }
    });

    this._monitorTelemetry();
  }

  _monitorTelemetry() {
    if (!this.device) return;
    this.telemetrySub = this.device.monitorCharacteristicForService(
      SERVICE_UUID,
      TELEMETRY_UUID,
      (error, characteristic) => {
        if (error) {
          this.emitter.emit('error', { stage: 'telemetry', error });
          return;
        }
        if (!characteristic || !characteristic.value) return;
        const raw = Buffer.from(characteristic.value, 'base64').toString('utf-8');
        try {
          const parsed = JSON.parse(raw);
          this.emitter.emit('telemetry', parsed);
        } catch (e) {
          // MTU diagnostika: pokud appka neposlala requestMTU:185, JSON prijde useknuty na 20B
          this.emitter.emit('error', {
            stage: 'telemetry-parse',
            error: e,
            raw,
            rawLength: raw.length,
            mtu: this.device ? this.device.mtu : null,
          });
        }
      },
    );
  }

  async disconnect() {
    if (this.device) {
      await this.device.cancelConnection();
    }
    this.device = null;
    this.setConnected(false);
  }

  async _writeRaw(str) {
    if (!this.device) throw new Error('BLE neni pripojeno');
    const b64 = Buffer.from(str, 'utf-8').toString('base64');
    await this.device.writeCharacteristicWithoutResponseForService(SERVICE_UUID, COMMAND_UUID, b64);
  }

  async sendCommand(str) {
    return this._writeRaw(str);
  }

  _chunkSize() {
    const mtu = (this.device && this.device.mtu) || 23;
    return Math.max(mtu - MAX_MTU_HEADROOM, 20);
  }

  async _sendPointBatches(points, prefix, beginCmd, endCmd) {
    this.mapReceiving = true;
    try {
      await this._writeRaw(beginCmd);
      const maxLen = this._chunkSize();
      let buf = '';
      let packetsSinceRest = 0;

      const flush = async () => {
        if (!buf.length) return;
        await this._writeRaw(prefix + buf);
        buf = '';
        packetsSinceRest += 1;
        if (packetsSinceRest % 4 === 0) {
          await new Promise((r) => setTimeout(r, 20));
        }
      };

      for (let i = 0; i < points.length; i++) {
        const entry = `${points[i].lat},${points[i].lng};`;
        if (prefix.length + buf.length + entry.length > maxLen) {
          await flush();
        }
        buf += entry;
      }
      await flush();
      await this._writeRaw(endCmd);
    } finally {
      this.mapReceiving = false;
    }
  }

  // trasa: max 280 bodu (appka redukuje pred volanim), protokol RT:BEGIN/RT:P/RT:END
  async sendRoute(points) {
    await this._writeRaw('RT:CLEAR');
    await this._sendPointBatches(points, 'RT:P:', 'RT:BEGIN', 'RT:END');
  }

  async clearRoute() {
    await this._writeRaw('RT:CLEAR');
  }

  // ulice: body oddelene segmentovym markerem {lat:999,lng:999}, protokol MP:BEGIN/MP:P/MP:END
  async sendStreets(points) {
    await this._sendPointBatches(points, 'MP:P:', 'MP:BEGIN', 'MP:END');
  }

  rideStart() { return this.sendCommand('RIDE:START'); }
  rideStop() { return this.sendCommand('RIDE:STOP'); }
  ridePause() { return this.sendCommand('RIDE:PAUSE'); }
  rideResume() { return this.sendCommand('RIDE:RESUME'); }
  resetTrip() { return this.sendCommand('RESET_TRIP'); }
  setTimezone(hours) { return this.sendCommand(`SET_TZ:${hours}`); }
  setScreen(n) { return this.sendCommand(`SCREEN:${n}`); }
  setFieldsConfig(indices) { return this.sendCommand(`CFG:FIELDS:${indices.join(',')}`); }
}

const instance = new CykloBleManager();
export default instance;
