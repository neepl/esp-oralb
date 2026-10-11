#include "ui.h"

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "config.h"
#include "touch.h"

using namespace oralb;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  LGFX() {
    {
      auto c = bus_.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = false;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = BOARD_TFT_SCLK;
      c.pin_mosi = BOARD_TFT_MOSI;
      c.pin_miso = BOARD_TFT_MISO;
      c.pin_dc = BOARD_TFT_DC;
      bus_.config(c);
      panel_.setBus(&bus_);
    }
    {
      auto c = panel_.config();
      c.pin_cs = BOARD_TFT_CS;
      c.pin_rst = BOARD_TFT_RST;
      c.pin_busy = -1;
      c.panel_width = SCREEN_W;
      c.panel_height = SCREEN_H;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = BOARD_TFT_OFFSET_ROTATION;
      c.readable = false;
      c.invert = BOARD_TFT_INVERT;
      c.rgb_order = BOARD_TFT_RGB_ORDER_BGR;
      c.dlen_16bit = false;
      c.bus_shared = false;
      panel_.config(c);
    }
    {
      auto c = light_.config();
      c.pin_bl = BOARD_TFT_BL;
      c.invert = false;
      c.freq = 12000;
      c.pwm_channel = 7;
      light_.config(c);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

namespace {

LGFX lcd;
lgfx::LGFX_Sprite band(&lcd);
constexpr int BAND_H = 12;  // 240 / 12 = 20 bands; one 320x12x16bpp sprite = 7.5 KB (TLS needs the heap; no PSRAM)

// Canvas for one band: screens draw in full-screen coordinates, the sprite is shifted by -oy.
struct BandCanvas : oralb::Canvas {
  lgfx::LGFX_Sprite& g;
  int oy = 0;
  uint32_t last_rgb = 0xFFFFFFFF;
  uint16_t last565 = 0;
  explicit BandCanvas(lgfx::LGFX_Sprite& s) : g(s) {}
  void fill_rect(int x, int y, int w, int h, uint32_t rgb) override {
    if (y + h <= oy || y >= oy + BAND_H) return;
    if (rgb != last_rgb) {
      last_rgb = rgb;
      last565 = g.color565((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    }
    g.fillRect(x, y - oy, w, h, last565);
  }
};

// Direct-to-panel canvas for the one-off message screen
struct LcdCanvas : oralb::Canvas {
  void fill_rect(int x, int y, int w, int h, uint32_t rgb) override {
    lcd.fillRect(x, y, w, h, lcd.color565((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
  }
};

}  // namespace

void ui_begin() {
  lcd.init();
  lcd.setRotation(DISPLAY_ROTATION);  // touch follows (LovyanGFX rotates both)
  touch_begin();  // own CST816S reader (see touch.h); no coordinates needed
  lcd.fillScreen(0);
  band.setColorDepth(16);
  band.setPsram(false);
  if (!band.createSprite(UI_W, BAND_H)) Serial.println("sprite alloc failed");
  lcd.setBrightness(BACKLIGHT_MAX);
}

void ui_set_brightness(uint8_t level) { lcd.setBrightness(level); }

bool ui_touch_down() { return touch_down(); }

void ui_render(const oralb::Scene& s) {
  BandCanvas c(band);
  for (int b = 0; b < UI_H / BAND_H; b++) {
    c.oy = b * BAND_H;
    oralb::draw_scene(c, s);  // each screen starts with a full-screen black fill
    band.pushSprite(&lcd, 0, b * BAND_H);
  }
}

void ui_message(const char* l1, const char* l2) {
  LcdCanvas c;
  oralb::draw_message(c, l1, l2);
}
