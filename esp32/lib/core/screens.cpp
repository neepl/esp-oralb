#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "gfx_assets.h"

namespace oralb {

using namespace gfx;

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const BigGlyph* big_glyph(char ch) {
  for (size_t i = 0; i < BIG_GLYPH_COUNT; i++)
    if (BIG_GLYPHS[i].ch == ch) return &BIG_GLYPHS[i];
  return nullptr;
}

int big_width(const char* t, int px) {
  int w = 0, n = 0;
  for (; *t; t++, n++) {
    const BigGlyph* g = big_glyph(*t);
    if (g) w += g->w * px;
  }
  return n ? w + px * (n - 1) : 0;
}

// Each font pixel is a px*px square with a thin gap (round(px * 0.12), at least 1).
void draw_big(Canvas& c, const char* t, int x, int y, int px, uint32_t color) {
  int g = (int)(px * 0.12 + 0.5);
  if (g < 1) g = 1;
  for (; *t; t++) {
    const BigGlyph* gl = big_glyph(*t);
    if (!gl) continue;
    for (int r = 0; r < 5; r++)
      for (int col = 0; col < gl->w; col++)
        if (gl->rows[r][col] == '1') c.fill_rect(x + col * px, y + r * px, px - g, px - g, color);
    x += gl->w * px + px;
  }
}

const TextGlyph* find_glyph(uint32_t cp) {
  size_t lo = 0, hi = TEXT_GLYPH_COUNT;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (TEXT_GLYPHS[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  return lo < TEXT_GLYPH_COUNT && TEXT_GLYPHS[lo].cp == cp ? &TEXT_GLYPHS[lo] : nullptr;
}

void draw_text(Canvas& c, const char* s, int x, int y, int scale, uint32_t color) {
  while (*s) {
    int cp = utf8_decode(s);
    const TextGlyph* g = find_glyph((uint32_t)cp);
    if (!g) g = find_glyph('?');
    for (int r = 0; r < 8; r++) {
      uint8_t bits = g->rows[r];
      for (int col = 0; col < 8;) {  // merge horizontal runs
        if (!(bits & (0x80 >> col))) {
          col++;
          continue;
        }
        int e = col;
        while (e < 8 && (bits & (0x80 >> e))) e++;
        c.fill_rect(x + col * scale, y + r * scale, (e - col) * scale, scale, color);
        col = e;
      }
    }
    x += 8 * scale;
  }
}

void draw_text_c(Canvas& c, const char* s, int cx, int y, int scale, uint32_t color) {
  draw_text(c, s, cx - text_width(s, scale) / 2, y, scale, color);
}

void draw_art(Canvas& c, const char* const* art, int x, int y, int px, uint32_t color, bool use_letters, int gap) {
  for (int r = 0; r < 16; r++)
    for (int col = 0; col < 16; col++) {
      char ch = art[r][col];
      if (ch == '.') continue;
      c.fill_rect(x + col * px, y + r * px, px - gap, px - gap, use_letters ? weather_color(ch) : color);
    }
}

void teeth_size(int cell, int& w, int& h) {
  w = TEETH_COLS * (cell + 2) - 2;
  h = TEETH_ROWS * (cell + 2) - 2;
}

void draw_teeth(Canvas& c, const Scene& s, int x, int y, int cell, bool highlight) {
  const int gap = 2;
  for (int z = 0; z < ZONE_COUNT; z++) {
    const TeethZone& tz = TEETH_ZONES[z];
    uint32_t col = (highlight && z == s.current_zone) ? C_ACCENT : (s.cov[z] >= 2 ? C_FG : (s.cov[z] == 1 ? C_DIM : C_OFF));
    for (int i = 0; i < tz.n; i++)
      c.fill_rect(x + tz.cells[i][0] * (cell + gap), y + tz.cells[i][1] * (cell + gap), cell, cell, col);
  }
}

void draw_progress(Canvas& c, int cx, int y, int seconds, bool blink_on) {
  const int size = 12, gap = 6, steps = 8;
  int done = clampi(seconds / 15, 0, steps);
  int total = steps * size + (steps - 1) * gap;
  int x = cx - total / 2;
  for (int i = 0; i < steps; i++) {
    uint32_t col;
    if (seconds >= 120) col = C_ACCENT;
    else if (i < done) col = C_FG;
    else if (i == done) col = blink_on ? C_ACCENT : C_OFF;
    else col = C_OFF;
    c.fill_rect(x, y, size, size, col);
    x += size + gap;
  }
}

void mmss(char* b, size_t n, int s) { snprintf(b, n, "%d:%02d", s / 60, s % 60); }

void clear(Canvas& c) { c.fill_rect(0, 0, c.width, c.height, C_BG); }

// Bottom-right block: siren icon, state, since, city. UNKNOWN: hidden.
void draw_alert(Canvas& c, const Scene& s) {
  if (s.alert == AlertLevel::UNKNOWN) return;
  bool none = s.alert == AlertLevel::NONE;
  // icon art, colour and label come from the generated ALERT_STYLES table (design.py)
  const AlertStyle& st = ALERT_STYLES[s.alert == AlertLevel::RED ? 0 : (s.alert == AlertLevel::YELLOW ? 1 : 2)];
  // vertically centred on the weather icon (design.py draw_alert)
  for (int r = 0; r < 16; r++)
    for (int col = 0; col < 16; col++) {
      char ch = st.art[r][col];
      if (ch != '.') c.fill_rect(196 + col * 3, 164 + r * 3, 3, 3, icon_color(ch, st.color));
    }
  bool full = !none && s.alert_since[0];
  draw_text(c, st.label, 250, full ? 172 : 180, 1, st.label_dim ? C_DIM : st.color);
  if (full) {
    char b[24];
    snprintf(b, sizeof b, "%s %s", STR_SINCE, s.alert_since);
    draw_text(c, b, 250, 186, 1, C_DIM);
  }
  draw_text(c, STR_ALERT_CITY, 250, full ? 200 : 194, 1, C_DIM);
}

void screen_clock(Canvas& c, const Scene& s) {
  clear(c);
  char t[8];
  if (s.time_valid) snprintf(t, sizeof t, "%02d%c%02d", s.hh, s.colon ? ':' : ' ', s.mm);  // same width
  else snprintf(t, sizeof t, "--:--");
  const int px = 13;
  draw_big(c, t, (SCREEN_W - big_width(t, px)) / 2, 22, px, C_FG);
  if (s.time_valid) {
    char d[48];
    snprintf(d, sizeof d, "%s, %d %s", DAYS[s.weekday % 7], s.day, MONTHS_GEN[(s.month + 11) % 12]);
    draw_text_c(c, d, SCREEN_W / 2, 112, 2, C_FG);
  }
  if (s.weather_valid) {
    draw_art(c, WEATHER_ART[(int)s.weather], 14, 156, 4, 0, true, 0);
    char tt[12];
    snprintf(tt, sizeof tt, "%+d\xC2\xB0", (int)s.temp);
    draw_text(c, tt, 86, 176, 3, C_FG);
  }
  draw_alert(c, s);
  if (s.status[0]) draw_text(c, s.status, 8, 228, 1, C_DIM);
}

void screen_brushing(Canvas& c, const Scene& s) {
  clear(c);
  char t[16];
  mmss(t, sizeof t, s.secs);
  const int px = 11;
  // Without a teeth chart (no ff0d) the timer + progress block (y 10..88) is centred vertically.
  const int dy = s.zones ? 0 : (SCREEN_H - 78) / 2 - 10;
  draw_big(c, t, (SCREEN_W - big_width(t, px)) / 2, 10 + dy, px,
           s.high_pressure ? C_WARN : (s.secs >= 120 ? C_ACCENT : C_FG));
  draw_progress(c, SCREEN_W / 2, 76 + dy, s.secs, s.blink_on);
  if (s.zones) {
    const int cell = 13;
    int tw, th;
    teeth_size(cell, tw, th);
    draw_teeth(c, s, (SCREEN_W - tw) / 2, 100, cell, true);
    draw_text(c, "\xD0\x9B", (SCREEN_W - tw) / 2 - 22, 100 + th / 2 - 4, 1, C_DIM);  // Л
    draw_text(c, "\xD0\x9F", (SCREEN_W + tw) / 2 + 14, 100 + th / 2 - 4, 1, C_DIM);  // П
  }
  if (s.high_pressure) draw_text(c, STR_PRESSURE, 8, SCREEN_H - 16, 1, C_WARN);
}

void screen_result(Canvas& c, const Scene& s) {
  clear(c);
  bool ok = s.secs >= 120;
  char t[16];
  mmss(t, sizeof t, s.secs);
  const int px = 8;
  // Without a teeth chart the face group (y 32..192) and the time are centred vertically.
  const int face_dy = s.zones ? 0 : (SCREEN_H - 160) / 2 - 32;
  draw_art(c, ok ? FACE_HAPPY : FACE_SAD, 8, 32 + face_dy, 8, C_ACCENT, false, 1);
  draw_text_c(c, ok ? STR_GREAT : STR_SHORT, 8 + 64, 176 + face_dy, 2, ok ? C_ACCENT : C_DIM);
  const int time_y = s.zones ? 18 : (SCREEN_H - 40) / 2;
  draw_big(c, t, 196 - big_width(t, px) / 2 + 30, time_y, px, C_FG);
  if (s.zones) {
    const int cell = 9;
    int tw, th;
    teeth_size(cell, tw, th);
    draw_teeth(c, s, 226 - tw / 2, 82, cell, false);
  }
}

}  // namespace

int utf8_decode(const char*& p) {
  const unsigned char* u = (const unsigned char*)p;
  int cp, n;
  if (u[0] < 0x80) { cp = u[0]; n = 1; }
  else if ((u[0] & 0xE0) == 0xC0) { cp = u[0] & 0x1F; n = 2; }
  else if ((u[0] & 0xF0) == 0xE0) { cp = u[0] & 0x0F; n = 3; }
  else if ((u[0] & 0xF8) == 0xF0) { cp = u[0] & 0x07; n = 4; }
  else { p++; return '?'; }
  for (int i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) { p += i; return '?'; }
    cp = (cp << 6) | (u[i] & 0x3F);
  }
  p += n;
  return cp;
}

int text_width(const char* s, int scale) {
  int n = 0;
  while (*s) {
    utf8_decode(s);
    n++;
  }
  return n * 8 * scale;
}

void draw_message(Canvas& c, const char* l1, const char* l2) {
  clear(c);
  draw_text_c(c, l1, c.width / 2, c.height / 2 - 20, 2, C_FG);
  draw_text_c(c, l2, c.width / 2, c.height / 2 + 8, 1, C_DIM);
}

void draw_scene(Canvas& c, const Scene& s) {
  switch (s.screen) {
    case Screen::BRUSHING: screen_brushing(c, s); break;
    case Screen::RESULT: screen_result(c, s); break;
    default: screen_clock(c, s); break;
  }
}

}  // namespace oralb
