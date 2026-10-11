// Settings web UI (http://esp-oralb.local/, basic auth). Everything here runs in the NETWORK task
// (the one doing the weather/alert fetches), so an HTTP request never overlaps a TLS handshake.
#pragma once

void web_poll();  // network task, every ~10 ms: start/stop the server as conditions change, serve pending requests
void web_stop();  // release port 80 (Wi-Fi setup portal); web_poll() restarts the server afterwards
