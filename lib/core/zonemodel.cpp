#include "zonemodel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace oralb {

namespace {
const char* jaw_name(char c) { return c == 'U' ? "upper" : "lower"; }
const char* side_name(char c) { return c == 'L' ? "left" : (c == 'F' ? "front" : "right"); }
const char* surf_name(char c) { return c == 'O' ? "outside" : (c == 'I' ? "inside" : "chewing"); }
}  // namespace

void zone_name(const char* z, char* buf, size_t n) {
  snprintf(buf, n, "%s %s %s", jaw_name(z[0]), side_name(z[2]), surf_name(z[1]));
}

int zone_index(const char* code) {
  for (int i = 0; i < ZONE_COUNT; i++)
    if (strncmp(ZONE_CODES[i], code, 3) == 0) return i;
  return -1;
}

// -- features -------------------------------------------------------------------------

bool imu12_feature(const ImuRaw* c, size_t n, double out[FEATURE_COUNT]) {
  if (n == 0) return false;
  double a_mean[3], g_mean[3];
  for (int i = 0; i < 3; i++) {
    double sa = 0, sg = 0;
    for (size_t k = 0; k < n; k++) {
      sa += c[k].a[i];
      sg += c[k].g[i] * GYRO_RAD_S;
    }
    a_mean[i] = sa / n;
    g_mean[i] = sg / n;
  }
  double norm = sqrt(a_mean[0] * a_mean[0] + a_mean[1] * a_mean[1] + a_mean[2] * a_mean[2]);
  if (norm < 1e-6) return false;
  for (int i = 0; i < 3; i++) {
    out[i] = a_mean[i] / norm;  // tilt
    out[3 + i] = g_mean[i];
    double sg = 0, sa = 0;
    for (size_t k = 0; k < n; k++) {
      double dg = c[k].g[i] * GYRO_RAD_S - g_mean[i];
      double da = c[k].a[i] - a_mean[i];
      sg += dg * dg;
      sa += da * da;
    }
    out[6 + i] = sqrt(sg / n);
    out[9 + i] = sqrt(sa / n) / ACCEL_UNITS_PER_G;
  }
  return true;
}

// -- model ----------------------------------------------------------------------------

void zone_loglik(const double x[FEATURE_COUNT], double out[ZONE_COUNT]) {
  double xs[FEATURE_COUNT];
  for (int i = 0; i < FEATURE_COUNT; i++) xs[i] = (x[i] - LDA_MEAN[i]) / LDA_SCALE[i];
  for (int z = 0; z < ZONE_COUNT; z++) {
    double s = 0;
    for (int i = 0; i < FEATURE_COUNT; i++) s += LDA_WEIGHTS[z][i] * xs[i];
    out[z] = s + LDA_BIAS[z];
  }
}

// -- filter -------------------------------------------------------------------------

ZoneFilter::ZoneFilter() {
  log_stay_ = log(STAY);
  log_move_ = log((1 - STAY) / (ZONE_COUNT - 1));
  reset();
}

void ZoneFilter::reset() {
  for (int z = 0; z < ZONE_COUNT; z++) belief_[z] = -log((double)ZONE_COUNT);
}

int ZoneFilter::update(const double x[FEATURE_COUNT]) {
  double ll[ZONE_COUNT];
  zone_loglik(x, ll);
  double top_ll = ll[0];
  for (int z = 1; z < ZONE_COUNT; z++)
    if (ll[z] > top_ll) top_ll = ll[z];
  // Cap per-window evidence so one odd window can't flip the zone on its own.
  for (int z = 0; z < ZONE_COUNT; z++)
    if (ll[z] < top_ll - EVIDENCE_CAP) ll[z] = top_ll - EVIDENCE_CAP;
  double best_prev = belief_[0];
  for (int z = 1; z < ZONE_COUNT; z++)
    if (belief_[z] > best_prev) best_prev = belief_[z];
  double nb[ZONE_COUNT];
  for (int z = 0; z < ZONE_COUNT; z++) {
    double stay = belief_[z] + log_stay_;
    double move = best_prev + log_move_;
    double hi = stay > move ? stay : move;
    nb[z] = hi + log(exp(stay - hi) + exp(move - hi)) + ll[z];
  }
  double top = nb[0];
  for (int z = 1; z < ZONE_COUNT; z++)
    if (nb[z] > top) top = nb[z];
  int best = 0;
  for (int z = 0; z < ZONE_COUNT; z++) {
    belief_[z] = nb[z] - top;
    if (belief_[z] > belief_[best]) best = z;  // strict: first maximum wins, like Python's max()
  }
  return best;
}

// -- tracker --------------------------------------------------------------------------

ZoneTracker::ZoneTracker() { reset(); }

void ZoneTracker::reset() {
  filter_.reset();
  for (int z = 0; z < ZONE_COUNT; z++) coverage[z] = 0;
  current = -1;
  count_ = 0;
  started_ = false;
  start_ = 0;
  samples = 0;
  windows = 0;
}

void ZoneTracker::add(double t, int ax, int ay, int az, int gx, int gy, int gz) {
  samples++;
  if (!started_) {
    start_ = t;
    started_ = true;
  }
  if (t - start_ >= WINDOW && count_ > 0) {
    double x[FEATURE_COUNT];
    bool ok = imu12_feature(chunk_, count_, x);
    count_ = 0;
    start_ = t;
    if (ok) {
      windows++;
      current = filter_.update(x);
      coverage[current] += WINDOW;
    }
  }
  if (count_ < MAX_WINDOW_SAMPLES) {
    ImuRaw& r = chunk_[count_++];
    r.a[0] = (int8_t)ax; r.a[1] = (int8_t)ay; r.a[2] = (int8_t)az;
    r.g[0] = (int8_t)gx; r.g[1] = (int8_t)gy; r.g[2] = (int8_t)gz;
  }
}

}  // namespace oralb
