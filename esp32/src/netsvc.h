// Network services available while Wi-Fi is up (WIFI_ALWAYS_ON): mDNS, telnet log server, ArduinoOTA.
#pragma once

void netsvc_start();  // idempotent; spawns the service task, which waits for the Wi-Fi connection
