// Keeps the clock across power loss and without internet (see "Time" in README). Source levels:
// none < restored < rtc < manual < ntp.
#pragma once
#include <stdint.h>

#include "timesrc.h"

void timekeep_boot();                          // setup(): keep a valid system time (rtc) or restore it from NVS
oralb::TimeSource timekeep_source();
void timekeep_ntp_synced();                    // SNTP callback (any task): source = ntp, save soon
bool timekeep_set_manual(int64_t epoch_utc);   // settimeofday + save, source = manual; false if implausible
void timekeep_tick();                          // network task: deferred save after NTP, periodic save (10 min)
void timekeep_save_now();                      // before every intentional restart
