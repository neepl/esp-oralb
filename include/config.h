// Build-time configuration for the esp-oralb firmware.
#pragma once

// POSIX TZ string for the clock (Europe/Kyiv). NTP gives UTC; this makes localtime() correct.
#define TZ_POSIX "EET-2EEST,M3.5.0/3,M10.5.0/4"

#define WIFI_AP_NAME "esp-oralb-setup"
#define WIFI_PORTAL_TIMEOUT_S 180  // on-demand portal (BOOT button); with no saved network it stays open
#define WIFI_CONNECT_TIMEOUT_S 30  // first connection at boot (BLE starts at the same time)
#define NTP_RESYNC_MS (6UL * 3600UL * 1000UL)  // brief Wi-Fi sessions after the first sync
#define NTP_RETRY_MS (10UL * 60UL * 1000UL)    // until the first successful sync
#define QUICK_RETRY_MS (30UL * 1000UL)         // first retries after a failed NTP/weather attempt
#define QUICK_RETRY_COUNT 5


#define BUTTON_LONG_MS 5000  // hold BOOT to open the Wi-Fi setup portal

// ff0b raw pressure value treated as "too hard" when adverts are unavailable (heuristic, as app.py)
#define BLE_SCAN_INTERVAL_MS 1000  // idle scan: 300 ms of every second (~30% radio duty)
#define BLE_SCAN_WINDOW_MS 300
#define BLE_SCAN_FAST_INTERVAL_MS 100  // brush awake (seen in the last BLE_AWAKE_HOLD_S): ~90% duty
#define BLE_SCAN_FAST_WINDOW_MS 90
#define BLE_AWAKE_HOLD_S 30
#define SYNTH_ADV_GAP_S 1.5  // adverts older than this -> synthesize frames from GATT
#define ZONE_RECONNECT_BACKOFF_S 3.0

// Weather (Open-Meteo current conditions, plain HTTP). Kyiv by default.
#define WEATHER_LAT 50.45
#define WEATHER_LON 30.52
#define WEATHER_REFRESH_MIN 30
#define WEATHER_MAX_AGE_MIN 180  // hide icon + temperature when the last successful fetch is older

// Night mode: on the clock screen with no brush activity the backlight turns fully off;
// a touch wakes it for NIGHT_WAKE_S seconds. Brush activity (BRUSHING/RESULT) turns it on.
#define NIGHT_START_H 23
#define NIGHT_END_H 7
#define NIGHT_WAKE_S 8

// Light sensor (CdS on GPIO34, ADC 0 dB). Typically high in the dark, near 0 in bright light:
// check the "ldr raw ... -> bl ..." log (every 10 s) and calibrate the two raw values.
#define LDR_BRIGHT_RAW 150   // reading at/below which the backlight is BACKLIGHT_MAX
#define LDR_DARK_RAW 1500    // reading at/above which the backlight is BACKLIGHT_MIN
#define BACKLIGHT_MIN 20
#define BACKLIGHT_MAX 255
#define LDR_SAMPLE_MS 200

// RGB status LED (usually common anode = active LOW). LED_MAX caps the PWM brightness (very bright LED).
#define LED_ACTIVE_LOW 1
#define LED_MAX 40
#define LED_SELF_TEST 1  // red, green, blue for 250 ms each at boot (checks wiring and polarity)

// Network. WIFI_ALWAYS_ON=1: stay connected in STA mode (USB-powered device): SNTP runs continuously,
// weather is fetched without switching Wi-Fi off, and mDNS / telnet log / OTA are available.
// 0: short Wi-Fi sessions (NTP + weather) followed by Wi-Fi off; no network services.
#define WIFI_ALWAYS_ON 1
#define HOSTNAME "esp-oralb"
#define TELNET_PORT 23
#define TELNET_MAX_CLIENTS 2
#define LOG_RING_LINES 32  // RAM: each line costs LOG_LINE_MAX bytes of static DRAM (= less heap)
#define LOG_LINE_MAX 100  // longer lines are truncated ('~')

// Air-raid alerts (siren.pp.ua API v3, HTTPS only, limit 50 req/min per IP). Region: "31" = м. Київ,
// "14" = Київська область. Polled only on the clock screen and only with WIFI_ALWAYS_ON.
#define ALERT_REGION_ID "31"
#define ALERT_POLL_S 15
#define HEAP_RESTART_BELOW 24000        // largest free block (TLS with dynamic buffers needs ~17-20 KB) ...
#define HEAP_RESTART_IDLE_MS 120000UL   // ... checked only after 2 min idle on the clock screen ...
#define HEAP_RESTART_LOW_MS 20000UL     // ... and only if it stays low this long (a TLS fetch takes 1-3 s)
#define ALERT_TLS_VERIFY 1  // 1: verify against the pinned roots (X1 + YR); 0: skip. (An earlier "X509 verification
                            // failed" was really out-of-memory during the RSA checks; see BAND_H / LOG_RING_LINES.)
                            // Was 0 while debugging: mbedTLS 2.28 rejects
                            // the siren.pp.ua chain (X509 verification failed) although OpenSSL accepts it.
                            // Encrypted but unauthenticated: someone on the LAN could spoof the alert state.
#define ALERT_MAX_AGE_S 120  // no successful poll for this long -> state unknown, block hidden
