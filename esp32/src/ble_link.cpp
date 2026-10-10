// GATT side (port of app.py zone_link): connect (scanning stays off while connected), subscribe to
// ff0d (IMU), ff08 (time), ff04 (state), ff0b (pressure); synthesize frames when adverts stop.
#include <NimBLEDevice.h>

#include "app_state.h"
#include "ble_internal.h"
#include "config.h"
#include "diag.h"
#include "imu.h"
#include "protocol.h"

namespace {

volatile uint32_t last_notify_ms = 0;  // any notification from the brush

struct Gatt {
  int state = 2;
  int secs = 0;
  int pressure_level = 0;  // ff0b byte 0 (brush's own: 2 = too much)
  char addr[20] = "";
} g;

// Stand in for adverts when the brush stops advertising while connected (like app.py synth()).
// Caller holds the Lock.
void synth() {
  double now = now_s();
  if (now - app.last_adv < SYNTH_ADV_GAP_S) return;  // adverts are flowing: primary source
  uint8_t status = g.pressure_level >= oralb::PRESSURE_LEVEL_HIGH ? 0x80 : 0;
  uint8_t raw[11] = {6, 50, 0, (uint8_t)g.state, status, (uint8_t)(g.secs / 60), (uint8_t)(g.secs % 60), 0, 0, 0, 0};
  oralb::BrushFrame f;
  if (oralb::parse(raw, sizeof raw, f)) app.session.on_frame(f, now, wall_s(), g.addr, nullptr);
}

void on_imu(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  last_notify_ms = millis();
  oralb::ImuSample s[4];
  size_t k = oralb::decode_sensor(d, n, s);
  static bool first = true;
  if (first) {
    first = false;
    DIAG("gatt: first ff0d notify (%u bytes, %u samples)", (unsigned)n, (unsigned)k);
  }
  if (!k) return;
  double t = now_s();
  Lock l;
  static double rate_t0 = 0;
  static uint32_t rate_n = 0;
  if (app.session.screen != oralb::Screen::BRUSHING) {
    rate_n = 0;
    rate_t0 = 0;
    return;
  }
  if (rate_t0 == 0) rate_t0 = t;
  rate_n += k;
  if (t - rate_t0 >= 5.0) {
    DIAG("imu %.1f samples/s", rate_n / (t - rate_t0));
    rate_n = 0;
    rate_t0 = t;
  }
  for (size_t i = 0; i < k; i++)
    app.tracker.add(t, s[i].accel[0], s[i].accel[1], s[i].accel[2], s[i].gyro[0], s[i].gyro[1], s[i].gyro[2]);
}

void on_time(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  last_notify_ms = millis();
  if (n < 2) return;
  static bool first = true;
  if (first) {
    first = false;
    DIAG("gatt: first ff08 notify %d:%02d", d[0], d[1]);
  }
  Lock l;
  g.secs = d[0] * 60 + d[1];
  synth();
}

void on_state(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  last_notify_ms = millis();
  if (n < 1) return;
  DIAG("gatt: ff04 state %d", d[0]);
  Lock l;
  g.state = d[0];
  synth();
}

void on_pressure(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  uint16_t raw;
  uint8_t level;
  if (!oralb::decode_pressure(d, n, raw, level)) return;
  if (level == g.pressure_level) return;
  // React at once (~25 notifications/s) instead of waiting for the next 1 Hz timer notification.
  Lock l;
  g.pressure_level = level;
  synth();
}

void set_status(const char* s) {
  Lock l;
  strncpy(app.zone_status, s, sizeof app.zone_status - 1);
}

// One connection attempt; returns when disconnected / failed.
void connect_once(NimBLEClient* c, const char* addr, uint8_t type) {
  strncpy(g.addr, addr, sizeof g.addr - 1);
  {
    // We only connect once the adverts say "running": start from that instead of the old default
    // (idle), or the first GATT timer notification would synthesize "idle" and end the session.
    Lock l;
    g.state = link_state.brush_state;
    g.secs = app.session.brush_seconds;
    g.pressure_level = 0;
  }
  DIAG("gatt: connect attempt %s", addr);
  set_status("connecting");
  NimBLEDevice::getScan()->stop();  // a connect and an active scan can't be started together
  // Ask for a short connection interval (7.5-30 ms) so discovery round trips are quick; the brush
  // may renegotiate. Units: 1.25 ms for the interval, 10 ms for the supervision timeout.
  c->setConnectionParams(6, 24, 0, 200);
  // deleteAttributes=false: keep the discovered services/characteristics of the (same) brush across
  // connections: no re-discovery next time and far less heap churn (fragmentation starved TLS).
  bool ok = c->connect(NimBLEAddress(std::string(addr), type), false);
  // No scan restart here: while connected the brush doesn't advertise, so scanning would only take
  // radio time from Wi-Fi. link_task restarts the (light) scan after the link ends.
  if (!ok) {
    DIAG("gatt: connect failed (err %d)", c->getLastError());
    set_status("connect failed");
    return;
  }
  DIAG("gatt: connected, discovering");
  uint32_t t_disc = millis();
  // Targeted discovery: only the ff00 service and the four characteristics we use (full discovery,
  // incl. the OTA service and every descriptor, took ~6 s). NimBLE caches what it finds and these
  // lookups never refresh, so the pointers (and their notify callbacks) stay valid.
  NimBLERemoteService* svc = c->getService(NimBLEUUID(oralb::SVC_MAIN));
  if (!svc) {
    DIAG("gatt: service ff00 not found (discovery failed?)");
    set_status("discovery failed");
    c->disconnect();
    return;
  }
  NimBLERemoteCharacteristic* imu = svc->getCharacteristic(NimBLEUUID(oralb::CH_SENSOR));
  if (!imu) {
    // zones unsupported on this brush: never retry, plain display only
    DIAG("gatt: no ff0d characteristic -> zones unsupported, no retries");
    app.zones_unsupported = true;
    set_status("no motion data");
    c->disconnect();
    return;
  }
  DIAG("gatt: ff0d present -> zones supported");
  // response=true: the iO ignores CCCD writes without response (NimBLE's default)
  bool ok_imu = imu->canNotify() && imu->subscribe(true, on_imu, true);
  NimBLERemoteCharacteristic* tch = svc->getCharacteristic(NimBLEUUID(oralb::CH_TIME));
  NimBLERemoteCharacteristic* sch = svc->getCharacteristic(NimBLEUUID(oralb::CH_STATE));
  NimBLERemoteCharacteristic* pch = svc->getCharacteristic(NimBLEUUID(oralb::CH_PRESSURE));
  if (tch && tch->canNotify()) tch->subscribe(true, on_time, true);
  if (sch && sch->canNotify()) sch->subscribe(true, on_state, true);
  if (pch && pch->canNotify()) pch->subscribe(true, on_pressure, true);
  if (sch) {  // current state right away (notifications only report changes)
    std::string v = std::string(sch->readValue());
    if (!v.empty()) {
      Lock l;
      g.state = (uint8_t)v[0];
      synth();
    }
  }
  if (!ok_imu) {
    DIAG("gatt: subscribe ff0d failed");
    set_status("subscribe failed");
    c->disconnect();
    return;
  }
  DIAG("gatt: subscribed ff0d/ff08/ff04/ff0b (%u ms after connect)", (unsigned)(millis() - t_disc));
  app.zones_linked = true;
  set_status("zones on");
  uint32_t last_poll = 0;
  bool poll_logged = false;
  while (c->isConnected() && !ble_paused) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t ms = millis();
    // Fallback: if the brush sends no notifications, poll time/state so the screen still works.
    if (ms - last_notify_ms > 1500 && ms - last_poll >= 500) {
      last_poll = ms;
      if (!poll_logged) {
        poll_logged = true;
        DIAG("gatt: no notifications -> polling ff04/ff08");
      }
      if (sch) {
        std::string v = std::string(sch->readValue());
        if (!v.empty()) {
          Lock l;
          g.state = (uint8_t)v[0];
          synth();
        }
      }
      if (tch) {
        std::string v = std::string(tch->readValue());
        if (v.size() >= 2) {
          Lock l;
          g.secs = (uint8_t)v[0] * 60 + (uint8_t)v[1];
          synth();
        }
      }
    }
  }
  app.zones_linked = false;
  DIAG("gatt: disconnected (err %d)", c->getLastError());
  set_status("disconnected");
}

void link_task(void*) {
  double backoff_until = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    double now = now_s();
    // Scan duty follows the brush: fast while it has been seen recently (awake), light otherwise.
    // Also keeps the scan alive (it stops when a connect attempt fails in odd ways).
    if (!link_state.connected && !ble_paused) {
      bool want_fast = now - link_state.seen_at < BLE_AWAKE_HOLD_S;
      if (!NimBLEDevice::getScan()->isScanning() || want_fast != ble_scan_is_fast()) ble_scan_start(want_fast);
    }
    if (app.zones_unsupported) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    char addr[20];
    uint8_t type;
    double seen;
    portENTER_CRITICAL(&link_state.mux);
    memcpy(addr, link_state.addr, sizeof addr);
    type = link_state.addr_type;
    seen = link_state.seen_at;
    portEXIT_CRITICAL(&link_state.mux);
    if (ble_paused) continue;
    if (!addr[0] || now - seen > 2.0 || now < backoff_until) continue;  // brush must be awake and advertising
    // Connect only once brushing starts: an idle-but-awake brush drops the link after ~20-30 s, and
    // each reconnect was a ~10 s blind spot (no scan while connecting). The brushing screen switches
    // from the advert itself; only the zone data waits for the link.
    if (link_state.brush_state != oralb::STATE_RUNNING) continue;
    link_state.connected = true;
    // One client for the lifetime of the firmware (create/delete per session fragmented the heap).
    static NimBLEClient* c = NimBLEDevice::createClient();
    c->setConnectTimeout(8);
    connect_once(c, addr, type);
    if (c->isConnected()) c->disconnect();
    link_state.connected = false;
    backoff_until = now_s() + ZONE_RECONNECT_BACKOFF_S;
  }
}

}  // namespace

void ble_link_start() { xTaskCreatePinnedToCore(link_task, "zone_link", 6144, nullptr, 1, nullptr, 1); }
