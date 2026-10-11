// Zone estimator: brush motion over a 0.5 s window -> brushing zone.
// Port of tools/python/src/esp_oralb/zonemodel.py (LinearZoneModel / "lda" path). No Arduino dependency.
//
// Per window: 12 features (tilt, gyro mean, gyro spread, accel spread), scored by an LDA
// (w_z . ((x - mean) / scale) + b_z); a sticky HMM filter smooths the per-window decision.
// Grip alignment is not used with the LDA model (Python only aligns the tilt-only model).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "zone_model_data.h"

namespace oralb {

constexpr double WINDOW = 0.5;        // seconds
constexpr double STAY = 0.9;          // HMM probability of staying in the same zone
constexpr double EVIDENCE_CAP = 4.0;  // max log-likelihood gap one window can contribute
constexpr double ZONE_TARGET_SECONDS = 120.0 / ZONE_COUNT;  // 7.5 s per zone = full coverage
constexpr size_t MAX_WINDOW_SAMPLES = 256;  // samples buffered per window (50 Hz -> ~25)

// One raw IMU sample (accelerometer and gyro raw counts; gyro zeros when unavailable).
struct ImuRaw {
  int8_t a[3];
  int8_t g[3];
};

// "UOL" -> "upper left outside"; buf needs >= 24 bytes.
void zone_name(const char* code, char* buf, size_t n);
// Index in the model's zone order, or -1.
int zone_index(const char* code);

// imu12 feature of a chunk: tilt (unit mean accel, 3) + mean gyro in rad/s (3) + gyro std (3)
// + accel std / 33 (3). False if the mean acceleration is ~zero (no tilt).
bool imu12_feature(const ImuRaw* chunk, size_t n, double out[FEATURE_COUNT]);

// LDA scores for every zone.
void zone_loglik(const double x[FEATURE_COUNT], double out[ZONE_COUNT]);

class ZoneFilter {
 public:
  ZoneFilter();
  void reset();
  // Feed one window feature; returns the most likely zone index.
  int update(const double x[FEATURE_COUNT]);

 private:
  double belief_[ZONE_COUNT];
  double log_stay_, log_move_;
};

class ZoneTracker {
 public:
  ZoneTracker();
  void reset();  // new session
  // One IMU sample at monotonic time t (seconds). Gyro is zeros for plain-motion packets.
  void add(double t, int ax, int ay, int az, int gx = 0, int gy = 0, int gz = 0);

  // state read by the UI
  int current = -1;                // zone index or -1
  double coverage[ZONE_COUNT];     // seconds per zone
  uint32_t samples = 0;            // IMU samples seen this session
  uint32_t windows = 0;            // features produced this session

 private:
  ZoneFilter filter_;
  ImuRaw chunk_[MAX_WINDOW_SAMPLES];
  size_t count_ = 0;
  bool started_ = false;
  double start_ = 0;
};

}  // namespace oralb
