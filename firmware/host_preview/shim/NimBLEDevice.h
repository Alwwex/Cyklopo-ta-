// NimBLE pro PC - jen prazdne kostry, aby slo prelozit UI.
#pragma once
#include "Arduino.h"
#include <string>
#include <functional>
namespace NIMBLE_PROPERTY { enum { READ = 0x02, WRITE_NR = 0x04, WRITE = 0x08, NOTIFY = 0x10 }; }
struct NimBLEConnInfo {};
class NimBLEUUID { public: NimBLEUUID(uint16_t) {} NimBLEUUID(const char*) {} };
class NimBLEAttValue {
public:
  NimBLEAttValue(const std::string& s = "") : s_(s) {}
  size_t length() const { return s_.size(); }
  const uint8_t* data() const { return (const uint8_t*)s_.data(); }
private: std::string s_;
};
class NimBLECharacteristic;
class NimBLECharacteristicCallbacks { public: virtual ~NimBLECharacteristicCallbacks() {} virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {} };
class NimBLECharacteristic {
public:
  void setCallbacks(NimBLECharacteristicCallbacks* c) { cb = c; }
  void setValue(const uint8_t* d, size_t n) { v.assign((const char*)d, n); }
  void setValue(const char* s) { v = s; }
  bool notify() { return true; }
  NimBLEAttValue getValue() { return NimBLEAttValue(v); }
  std::string v;
  NimBLECharacteristicCallbacks* cb = nullptr;
};
class NimBLEService { public: NimBLECharacteristic* createCharacteristic(const char*, uint32_t, uint16_t = 512) { return new NimBLECharacteristic(); } };
class NimBLEServer;
class NimBLEServerCallbacks { public: virtual ~NimBLEServerCallbacks() {} virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {} virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {} };
class NimBLEServer { public: void setCallbacks(NimBLEServerCallbacks*) {} NimBLEService* createService(const char*) { return new NimBLEService(); } uint8_t getConnectedCount() const { return 0; } };
class NimBLEAdvertisementData { public: void setName(const std::string&) {} };
class NimBLEAdvertising { public: bool addServiceUUID(const char*) { return true; } bool setScanResponseData(const NimBLEAdvertisementData&) { return true; } void enableScanResponse(bool) {} bool start() { return true; } };
class NimBLEAdvertisedDevice { public: bool isAdvertisingService(const NimBLEUUID&) const { return false; } };
class NimBLEScanResults { public: int getCount() const { return 0; } const NimBLEAdvertisedDevice* getDevice(uint32_t) const { return nullptr; } };
class NimBLEScan { public: void setActiveScan(bool) {} NimBLEScanResults getResults(uint32_t, bool = false) { return {}; } void clearResults() {} };
class NimBLERemoteCharacteristic {
public:
  typedef std::function<void(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)> notify_callback;
  bool canNotify() const { return false; }
  bool subscribe(bool, const notify_callback = nullptr, bool = true) const { return false; }
};
class NimBLERemoteService { public: NimBLERemoteCharacteristic* getCharacteristic(const NimBLEUUID&) { return nullptr; } };
class NimBLEClient {
public:
  void setConnectTimeout(uint32_t) {}
  bool connect(const NimBLEAdvertisedDevice*) { return false; }
  NimBLERemoteService* getService(const NimBLEUUID&) { return nullptr; }
  bool isConnected() { return false; }
  int disconnect() { return 0; }
};
class NimBLEDevice {
public:
  static void init(const std::string&) {}
  static void setMTU(uint16_t) {}
  static NimBLEServer* createServer() { static NimBLEServer s; return &s; }
  static NimBLEAdvertising* getAdvertising() { static NimBLEAdvertising a; return &a; }
  static bool startAdvertising() { return true; }
  static NimBLEScan* getScan() { static NimBLEScan s; return &s; }
  static NimBLEClient* getDisconnectedClient() { return nullptr; }
  static NimBLEClient* createClient() { return nullptr; }
  static void deinit(bool) {}
};
