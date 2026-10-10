#include "netsvc.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>

#include "app_state.h"
#include "ble.h"
#include "config.h"
#include "diag.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {

// ---- telnet log server on raw non-blocking lwip sockets: never stalls anyone --------------

struct Client {
  int fd = -1;
  uint32_t cursor = 0;
  char pend[LOG_LINE_MAX + 40];  // line (plus CRLF) not yet fully sent
  size_t plen = 0, poff = 0;
};
Client clients[TELNET_MAX_CLIENTS];
int listen_fd = -1;

void set_nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

void close_client(Client& c) {
  if (c.fd >= 0) close(c.fd);
  c.fd = -1;
  c.plen = c.poff = 0;
}

void telnet_begin() {
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) return;
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(TELNET_PORT);
  int yes = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  if (bind(listen_fd, (sockaddr*)&a, sizeof a) < 0 || listen(listen_fd, 1) < 0) {
    close(listen_fd);
    listen_fd = -1;
    return;
  }
  set_nonblocking(listen_fd);
}

// returns false if the connection died
bool flush_pending(Client& c) {
  while (c.poff < c.plen) {
    int r = send(c.fd, c.pend + c.poff, c.plen - c.poff, MSG_DONTWAIT);
    if (r > 0) {
      c.poff += (size_t)r;
    } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;  // buffer full: try again later (the ring keeps moving; we may skip lines)
    } else {
      return false;
    }
  }
  c.plen = c.poff = 0;
  return true;
}

void telnet_pump() {
  if (listen_fd < 0) return;
  for (;;) {
    int fd = accept(listen_fd, nullptr, nullptr);
    if (fd < 0) break;
    Client* slot = nullptr;
    for (auto& c : clients)
      if (c.fd < 0) { slot = &c; break; }
    if (!slot) {
      static const char busy[] = "esp-oralb: too many log clients\r\n";
      send(fd, busy, sizeof busy - 1, MSG_DONTWAIT);
      close(fd);
      continue;
    }
    set_nonblocking(fd);
    slot->fd = fd;
    slot->cursor = diag_ring_oldest();  // replay the ring, then stream
    slot->plen = slot->poff = 0;
  }
  for (auto& c : clients) {
    if (c.fd < 0) continue;
    char in[32];
    int r = recv(c.fd, in, sizeof in, MSG_DONTWAIT);  // discard input, detect close
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      close_client(c);
      continue;
    }
    if (!flush_pending(c)) { close_client(c); continue; }
    for (int n = 0; n < 24 && c.plen == 0; n++) {
      uint32_t skipped = 0;
      char line[LOG_LINE_MAX + 1];
      int len = diag_ring_next(c.cursor, line, sizeof line, &skipped);
      if (len < 0 && skipped == 0) break;
      if (skipped) c.plen = (size_t)snprintf(c.pend, sizeof c.pend, "[%u lines dropped]\r\n", (unsigned)skipped);
      else c.plen = (size_t)snprintf(c.pend, sizeof c.pend, "%s\r\n", line);
      if (len > 0 && skipped) c.cursor--;  // deliver the line after the notice
      c.poff = 0;
      if (!flush_pending(c)) { close_client(c); break; }
    }
  }
}

// ---- OTA ----------------------------------------------------------------------------------

int last_pct = -1;

void ota_begin() {
  ArduinoOTA.setHostname(HOSTNAME);  // ArduinoOTA also announces _arduino._tcp over mDNS
  ArduinoOTA.setPassword(OTA_PASSWORD);
  // The callbacks run in this (network) task: they only publish state. Drawing from here raced the
  // main loop's rendering and corrupted the display bus mutex (assert in xQueueGenericSend).
  ArduinoOTA.onStart([]() {
    app.ota_pct = 0;
    app.ota_error_ms = 0;
    app.ota_active = true;
    ble_pause(true);  // free radio time and heap: stop scanning, drop the GATT link
    last_pct = -1;
    DIAG("ota: start (%s)", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    if (pct == last_pct) return;
    if (last_pct < 0 || pct / 10 != last_pct / 10) DIAG("ota: %d%%", pct);
    last_pct = pct;
    app.ota_pct = pct;
  });
  ArduinoOTA.onEnd([]() { DIAG("ota: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) {
    DIAG("ota: error %u", (unsigned)e);
    app.ota_error_ms = millis();  // the main loop shows the error for 3 s, then resumes
    ble_pause(false);
  });
  ArduinoOTA.begin();
}

void svc_task(void*) {
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
  ota_begin();  // starts mDNS (hostname.local) and the OTA listener
  MDNS.addService("telnet", "tcp", TELNET_PORT);
  telnet_begin();
  DIAG("net: services up: %s.local, telnet %d, OTA", HOSTNAME, TELNET_PORT);
  DIAG("heap free %u largest %u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  for (;;) {
    ArduinoOTA.handle();
    telnet_pump();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void netsvc_start() {
  static bool started = false;
  if (started) return;
  started = true;
  xTaskCreatePinnedToCore(svc_task, "netsvc", 6144, nullptr, 1, nullptr, 1);
}
