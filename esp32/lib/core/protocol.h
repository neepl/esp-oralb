// Oral-B BLE advertisement decoder (manufacturer data, company ID 0x00DC).
// Port of python/src/esp_oralb/protocol.py. No Arduino dependency.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace oralb {

constexpr uint16_t COMPANY_ID = 0x00DC;
constexpr uint8_t STATE_RUNNING = 3;

struct BrushFrame {
  uint8_t protocol = 0;
  uint8_t model_id = 0;
  uint8_t state = 0;
  uint8_t status = 0;
  uint8_t minutes = 0;
  uint8_t seconds = 0;
  uint8_t mode = 0;
  uint8_t sector_raw = 0;
  bool has_sector_info = false;  // 11-byte frames only
  uint8_t sector_timer = 0;
  uint8_t sector_count = 0;

  int brush_seconds() const { return minutes * 60 + seconds; }
  bool running() const { return state == STATE_RUNNING; }
  // Button flags take precedence, matching oralb-ble's _decode_pressure.
  bool high_pressure() const { return !(status & 0x0C) && (status & 0x80); }
  bool button_pressed() const { return (status & 0x0C) != 0; }
  int sectors() const {
    int n = (has_sector_info ? sector_count : 0) & 0x07;
    return n ? n : 4;
  }
  int sector() const {
    if (!running()) return 0;
    int q = sector_raw & 0x07;
    return q == 7 ? sectors() : q;
  }
  const char* state_name() const;
  const char* mode_name() const;
  const char* model_name() const;
};

bool is_io(uint8_t model_id);
const char* model_name(uint8_t model_id);

// Decode the manufacturer payload (company ID already stripped). False if length is not 9 or 11.
bool parse(const uint8_t* data, size_t len, BrushFrame& out);

// NimBLE hands over the raw manufacturer field including the 2 little-endian company-ID
// bytes. Returns true and sets payload/len (stripped) iff the company ID is Oral-B's.
bool strip_company_id(const uint8_t* mfr, size_t len, const uint8_t*& payload, size_t& payload_len);

}  // namespace oralb
