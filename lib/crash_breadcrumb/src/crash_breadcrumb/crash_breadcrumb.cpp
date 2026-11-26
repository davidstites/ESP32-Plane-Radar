#include "crash_breadcrumb/crash_breadcrumb.h"

#include <cstdio>
#include <cstring>

namespace crash_breadcrumb {

namespace {

bool printable(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return u >= 0x20 && u <= 0x7E;
}

// Appends to a bounded buffer; once anything does not fit, ok stays false.
class Writer {
 public:
  Writer(char* out, size_t cap) : out_(out), cap_(cap) {}

  void put(char c) {
    if (len_ + 1 >= cap_) {
      ok_ = false;
      return;
    }
    out_[len_++] = c;
  }
  void raw(const char* s) {
    while (*s) put(*s++);
  }
  void number(uint32_t v) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(v));
    raw(buf);
  }
  void quoted(const char* s) {
    put('"');
    for (; s && *s; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        put('\\');
        put(static_cast<char>(c));
      } else if (c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        raw(buf);
      } else {
        put(static_cast<char>(c));
      }
    }
    put('"');
  }
  size_t finish() {
    if (!ok_ || cap_ == 0) return 0;
    out_[len_] = '\0';
    return len_;
  }

 private:
  char* out_;
  size_t cap_;
  size_t len_ = 0;
  bool ok_ = true;
};

}  // namespace

void set(Breadcrumb& b, const char* label, uint32_t uptimeMs) {
  memset(b.label, 0, sizeof(b.label));
  strncpy(b.label, label, kLabelMax - 1);
  b.uptimeMs = uptimeMs;
  b.magic = kMagic;
}

size_t copyLabel(const Breadcrumb& b, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (b.magic != kMagic) return 0;
  size_t len = 0;
  while (len < kLabelMax && b.label[len] != '\0') {
    if (!printable(b.label[len])) return 0;
    ++len;
  }
  if (len == kLabelMax) return 0;  // never terminated: not a label we wrote
  const size_t n = len < cap - 1 ? len : cap - 1;
  memcpy(out, b.label, n);
  out[n] = '\0';
  return n;
}

size_t formatReportJson(const Report& r, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  Writer w(out, cap);
  w.raw("{\"reset\":");
  w.quoted(r.resetReason);
  w.raw(",\"checkpoint\":");
  w.quoted(r.checkpoint);
  w.raw(",\"checkpoint_ms\":");
  w.number(r.checkpointUptimeMs);
  w.raw(",\"build\":");
  w.number(r.runningBuild);
  w.raw(",\"pending\":");
  w.number(r.pendingBuild);
  w.raw(",\"blacklist\":[");
  const size_t count = r.blacklistCount < kMaxBlacklist ? r.blacklistCount : kMaxBlacklist;
  for (size_t i = 0; i < count; ++i) {
    if (i) w.put(',');
    w.number(r.blacklist[i]);
  }
  w.raw("],\"heap_free\":");
  w.number(r.heapFree);
  w.raw(",\"heap_min\":");
  w.number(r.heapLowest);
  w.raw(",\"slot\":");
  w.quoted(r.slot);
  w.put('}');
  return w.finish();
}

}  // namespace crash_breadcrumb
