#include "protocol.h"

namespace oralb {

namespace {
struct Name {
  int id;
  const char* name;
};

const char* lookup(const Name* t, size_t n, int id) {
  for (size_t i = 0; i < n; i++)
    if (t[i].id == id) return t[i].name;
  return nullptr;
}

const Name STATES[] = {
    {0, "unknown"},        {1, "initializing"}, {2, "idle"},     {3, "running"},
    {4, "charging"},       {5, "setup"},        {6, "flight menu"}, {8, "selection menu"},
    {9, "off"},            {10, "post brushing statistics"},      {113, "final test"},
    {114, "pcb test"},     {115, "sleeping"},   {116, "transport"},
};
const Name SMART_MODES[] = {
    {0, "off"},     {1, "daily clean"},      {2, "sensitive"}, {3, "massage"},
    {4, "whitening"}, {5, "deep clean"},     {6, "tongue cleaning"}, {7, "turbo"},
};
const Name IO_MODES[] = {
    {0, "daily clean"}, {1, "sensitive"},       {2, "gum care"}, {3, "whiten"},
    {4, "intense"},     {5, "super sensitive"}, {6, "tongue cleaning"},
    {8, "settings"},    {9, "off"},             {11, "smart adapt"},
};
}  // namespace

bool is_io(uint8_t id) { return id >= 48 && id <= 54; }

const char* model_name(uint8_t id) {
  if (id <= 2) return "Triumph D36";
  if (id >= 32 && id <= 34) return "Genius D701";
  if (id >= 39 && id <= 41) return "Smart D700";
  if (id >= 48 && id <= 51) return "iO Series";
  if (id == 52) return "iO Series 4";
  if (id == 53) return "iO Series 5";
  if (id == 54) return "iO Series";
  if (id >= 64 && id <= 70) return "Smart D21";
  if (id >= 80 && id <= 87) return "Pro D601";
  if (id >= 112 && id <= 119) return "Genius X D706";
  return "Oral-B";
}

const char* BrushFrame::state_name() const {
  const char* n = lookup(STATES, sizeof STATES / sizeof *STATES, state);
  return n ? n : "state ?";
}
const char* BrushFrame::mode_name() const {
  const char* n = is_io(model_id) ? lookup(IO_MODES, sizeof IO_MODES / sizeof *IO_MODES, mode)
                                  : lookup(SMART_MODES, sizeof SMART_MODES / sizeof *SMART_MODES, mode);
  return n ? n : "mode ?";
}
const char* BrushFrame::model_name() const { return oralb::model_name(model_id); }

bool parse(const uint8_t* d, size_t len, BrushFrame& f) {
  if (len != 9 && len != 11) return false;
  f.protocol = d[0];
  f.model_id = d[1];
  f.state = d[3];
  f.status = d[4];
  f.minutes = d[5];
  f.seconds = d[6];
  f.mode = d[7];
  f.sector_raw = d[8];
  f.has_sector_info = len == 11;
  f.sector_timer = f.has_sector_info ? d[9] : 0;
  f.sector_count = f.has_sector_info ? d[10] : 0;
  return true;
}

bool strip_company_id(const uint8_t* m, size_t len, const uint8_t*& payload, size_t& n) {
  if (len < 2) return false;
  if ((uint16_t)(m[0] | (m[1] << 8)) != COMPANY_ID) return false;
  payload = m + 2;
  n = len - 2;
  return true;
}

}  // namespace oralb
