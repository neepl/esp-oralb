// Wi-Fi + NTP + weather for the clock screen. Wi-Fi is only on briefly (setup portal, first sync,
// periodic refresh while the clock is showing); the brush screens never depend on it.
#pragma once
#include <stdint.h>

#include "alert.h"
#include "settings.h"
#include "weather.h"

enum class NetState { CONNECTING, PORTAL, SYNCED, OFFLINE };

void net_begin();            // spawns the network task (setup portal only when no network is saved)
NetState net_state();
bool net_time_valid();       // the clock has been set by any source (not only NTP)
// Latest successful weather and its age; false until the first successful fetch.
bool net_weather(oralb::WeatherData& w, uint32_t& age_ms);
// Latest air-raid state (WIFI_ALWAYS_ON only), `since` = "HH:MM" local or empty, and the age of the last
// successful poll; false until the first success.
bool net_alert(oralb::AlertLevel& level, char since[8], uint32_t& age_ms);
void net_request_portal();   // open the Wi-Fi setup portal (BOOT button); keeps the saved network

// Time is displayable once any source (restored / rtc / manual / ntp) has set it; see timekeep.h.
bool net_ntp_synced();            // at least one NTP sync since boot
int net_weather_last_code();      // HTTP status of the latest weather fetch (0 none yet)
bool net_weather_last_ok();
int net_alert_last_code();        // HTTP status of the latest alert poll (-1 transport, -1000 bad reply, 0 none yet)
// New settings were committed (called from the network task): refetch on a new location/region, apply the TZ.
void net_on_settings(const oralb::Settings& old_s, const oralb::Settings& new_s);
