// Wi-Fi + NTP + weather for the clock screen. Wi-Fi is only on briefly (setup portal, first sync,
// periodic refresh while the clock is showing); the brush screens never depend on it.
#pragma once
#include <stdint.h>

#include "alert.h"
#include "weather.h"

enum class NetState { CONNECTING, PORTAL, SYNCED, OFFLINE };

void net_begin();            // spawns the network task (setup portal only when no network is saved)
NetState net_state();
bool net_time_valid();       // NTP has set the clock
// Latest successful weather and its age; false until the first successful fetch.
bool net_weather(oralb::WeatherData& w, uint32_t& age_ms);
// Latest air-raid state (WIFI_ALWAYS_ON only), `since` = "HH:MM" local or empty, and the age of the last
// successful poll; false until the first success.
bool net_alert(oralb::AlertLevel& level, char since[8], uint32_t& age_ms);
void net_request_portal();   // open the Wi-Fi setup portal (BOOT button); keeps the saved network
