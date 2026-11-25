// Test doubles for the OTA HAL. Each overrides behaviour through std::function members that
// default to benign success, so a test sets only the hook it cares about. `log`, when set,
// receives one line per mutating call so tests can assert ordering across fakes.
#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ota_core/hal.h"

namespace ota_fakes {

using Log = std::vector<std::string>;

class FakeStore : public ota_core::IOtaStore {
 public:
  bool okValue = true;
  bool failWrites = false;
  int writeCount = 0;
  Log* log = nullptr;
  std::map<std::string, uint32_t> uints;
  std::map<std::string, std::vector<uint8_t>> blobs;

  bool ok() const override { return okValue; }
  uint32_t getUInt(const char* key, uint32_t def) override {
    auto it = uints.find(key);
    return it == uints.end() ? def : it->second;
  }
  bool putUInt(const char* key, uint32_t v) override {
    if (!record("putUInt:", key)) return false;
    uints[key] = v;
    return true;
  }
  size_t getBytes(const char* key, void* buf, size_t cap) override {
    auto it = blobs.find(key);
    if (it == blobs.end() || it->second.size() > cap) return 0;
    memcpy(buf, it->second.data(), it->second.size());
    return it->second.size();
  }
  bool putBytes(const char* key, const void* buf, size_t len) override {
    if (!record("putBytes:", key)) return false;
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    blobs[key] = std::vector<uint8_t>(p, p + len);
    return true;
  }
  bool erase(const char* key) override {
    if (!record("erase:", key)) return false;
    uints.erase(key);
    blobs.erase(key);
    return true;
  }

 private:
  bool record(const char* what, const char* key) {
    if (!okValue || failWrites) return false;
    ++writeCount;
    if (log) log->push_back(std::string(what) + key);
    return true;
  }
};

class FakeSlots : public ota_core::IOtaSlots {
 public:
  ota_core::SlotState state = ota_core::SlotState::kValid;
  bool invalidSlot = false;
  bool markFails = false;
  bool beginFails = false;
  bool endFails = false;
  size_t slotCapacity = 0x1F0000;
  size_t writeLimit = static_cast<size_t>(-1);  // total bytes accepted before writes short
  std::vector<uint8_t> written;
  bool active = false;
  Log* log = nullptr;
  std::function<size_t(const uint8_t*, size_t)> onWrite;

  ota_core::SlotState runningState() override { return state; }
  bool hasInvalidSlot() override { return invalidSlot; }
  bool markRunningValid() override {
    note("markRunningValid");
    if (markFails) return false;
    state = ota_core::SlotState::kValid;
    return true;
  }
  bool beginUpdate(size_t size) override {
    note("beginUpdate");
    if (beginFails || size > slotCapacity) return false;
    written.clear();
    active = true;
    return true;
  }
  size_t writeUpdate(const uint8_t* data, size_t len) override {
    if (onWrite) return onWrite(data, len);
    const size_t room = writeLimit > written.size() ? writeLimit - written.size() : 0;
    const size_t n = std::min(len, room);
    written.insert(written.end(), data, data + n);
    return n;
  }
  bool endUpdate() override {
    note("endUpdate");
    ++endCalls;
    active = false;
    return !endFails;
  }
  void abortUpdate() override {
    note("abortUpdate");
    active = false;
  }
  int endCalls = 0;

 private:
  void note(const char* what) {
    if (log) log->push_back(what);
  }
};

class FakeHttp : public ota_core::IOtaHttp {
 public:
  struct Manifest {
    int status = 200;
    std::string etag;
    std::string body;
    bool tooLarge = false;
    bool transportFails = false;
  };
  std::map<std::string, Manifest> manifests;  // by path; a missing path is transport failure
  std::vector<std::string> fetchedPaths;
  std::vector<std::string> ifNoneMatchSent;

  int64_t openResult = -1;  // set to the content length (or a negative status) to open
  std::string image;
  size_t imageLimit = static_cast<size_t>(-1);  // readImage returns -1 after this many bytes
  std::vector<std::string> openedPaths;
  std::vector<size_t> readCaps;  // the buffer size of each readImage call
  bool closed = false;

  bool fetchManifest(const char* path, const char* ifNoneMatch, ota_core::ManifestResponse& out) override {
    fetchedPaths.push_back(path);
    ifNoneMatchSent.push_back(ifNoneMatch ? ifNoneMatch : "");
    auto it = manifests.find(path);
    if (it == manifests.end() || it->second.transportFails) return false;
    const Manifest& m = it->second;
    memset(&out, 0, sizeof(out));
    out.status = m.status;
    strncpy(out.etag, m.etag.c_str(), sizeof(out.etag) - 1);
    out.tooLarge = m.tooLarge;
    out.bodyLen = std::min(m.body.size(), ota_core::kMaxManifestBytes);
    memcpy(out.body, m.body.data(), out.bodyLen);
    return true;
  }
  int64_t openImage(const char* path) override {
    openedPaths.push_back(path);
    pos_ = 0;
    return openResult;
  }
  int readImage(uint8_t* buf, size_t cap) override {
    readCaps.push_back(cap);
    if (pos_ >= imageLimit) return -1;
    const size_t end = std::min(image.size(), imageLimit);
    const size_t n = std::min(cap, end - std::min(pos_, end));
    memcpy(buf, image.data() + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }
  void closeImage() override { closed = true; }

 private:
  size_t pos_ = 0;
};

}  // namespace ota_fakes
