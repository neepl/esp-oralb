// Weather helpers (no Arduino dependency): WMO code -> icon, Open-Meteo JSON parsing, day/night.
#pragma once

namespace oralb {

// Order matches WEATHER_ART in gfx_assets.h
enum class Icon : unsigned char { CLEAR, PARTLY, CLOUDY, FOG, RAIN, SNOW, STORM, CLEAR_NIGHT, PARTLY_NIGHT };

// 0-1 clear; 2 partly; 3 cloudy; 45/48 fog; 51-67 and 80-82 rain; 71-77 and 85-86 snow; 95-99 storm.
// Unknown codes map to CLOUDY. (Day variants only; see weather_icon() for the night swap.)
Icon icon_from_wmo(int code);

struct WeatherData {
  int code = 0;
  float temp_c = 0;
  bool has_is_day = false;  // current.is_day from the same reply
  bool is_day = true;
  bool has_sun = false;     // today's sunrise/sunset (local time)
  int sunrise_min = 0;      // minutes since local midnight
  int sunset_min = 0;
};

// Parse an Open-Meteo reply: current.temperature_2m / weather_code (required), current.is_day and
// daily.sunrise[0] / daily.sunset[0] ("2026-10-10T06:58", local time) when present.
bool parse_open_meteo(const char* json, WeatherData& out);

// Daytime decision for local minute-of-day `now_min`: sunrise <= now < sunset when sun times are known;
// else the reply's is_day; else 07:00-19:00. With no valid clock only is_day / "day" are used.
bool is_daytime(const WeatherData& w, int now_min, bool clock_valid);

// Icon to show: CLEAR/PARTLY switch to their night variants after sunset / before sunrise.
Icon weather_icon(const WeatherData& w, int now_min, bool clock_valid);

}  // namespace oralb
