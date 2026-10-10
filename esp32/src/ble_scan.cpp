// Scan side: filter company ID 0x00DC, decode, feed the session state machine.
#include <NimBLEDevice.h>

#include "app_state.h"
#include "ble.h"
#include "ble_internal.h"
#include "config.h"
#include "diag.h"
#include "protocol.h"

AppState app;
BleLink link_state;
volatile bool ble_paused = false;

void ble_pause(bool paused) {
  ble_paused = paused;
  if (paused) NimBLEDevice::getScan()->stop();  // the link task drops the GATT connection and stays idle
}

namespace {

class AdvCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (!dev->haveManufacturerData()) return;
    std::string m = dev->getManufacturerData();
    const uint8_t* payload;
    size_t n;
    if (!oralb::strip_company_id((const uint8_t*)m.data(), m.size(), payload, n)) return;
    oralb::BrushFrame f;
    if (!oralb::parse(payload, n, f)) return;
    std::string addr = dev->getAddress().toString();
    int rssi = dev->getRSSI();
    static bool first = true;
    if (first) {
      first = false;
      DIAG("first Oral-B advert: %s model=%s(%d) rssi=%d", addr.c_str(), f.model_name(), f.model_id, rssi);
    }
    double now = now_s();
    {
      Lock l;
      app.last_adv = now;
      app.session.on_frame(f, now, wall_s(), addr.c_str(), &rssi);
    }
    // remember the brush so the link task can connect to it
    if (!link_state.connected) {
      portENTER_CRITICAL(&link_state.mux);
      strncpy(link_state.addr, addr.c_str(), sizeof link_state.addr - 1);
      link_state.addr_type = dev->getAddressType();
      link_state.seen_at = now;
      link_state.brush_state = f.state;
      portEXIT_CRITICAL(&link_state.mux);
    }
  }
};

AdvCallbacks adv_cb;

}  // namespace

namespace {
bool scan_fast = false;
}

// Two duty levels: light while the brush sleeps (it advertises ~8x/s once awake, so 300 ms per second
// still finds it within ~1 s and Wi-Fi keeps most of the shared radio), fast while it is awake so the
// switch to "running" is caught at once. No scanning at all while connected.
void ble_scan_start(bool fast) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  scan_fast = fast;
  scan->setAdvertisedDeviceCallbacks(&adv_cb, true);  // want duplicates: ~8 adverts/s
  scan->setActiveScan(false);
  scan->setInterval(fast ? BLE_SCAN_FAST_INTERVAL_MS : BLE_SCAN_INTERVAL_MS);
  scan->setWindow(fast ? BLE_SCAN_FAST_WINDOW_MS : BLE_SCAN_WINDOW_MS);
  scan->start(0, nullptr, false);  // 0 = until stopped
  DIAG("ble: scan %s", fast ? "fast (brush awake)" : "light");
}

bool ble_scan_is_fast() { return scan_fast; }

void ble_begin() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  ble_scan_start(false);
  ble_link_start();
}
