#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/manifest.h"

namespace ota_core {

/** Small persistent key/value store (NVS on the device). */
class IOtaStore {
 public:
  /** False when the store could not be opened. Callers fail closed: no install, no writes. */
  virtual bool ok() const = 0;
  virtual uint32_t getUInt(const char* key, uint32_t def) = 0;
  virtual bool putUInt(const char* key, uint32_t v) = 0;
  /** Returns the stored length, or 0 if the key is absent or does not fit `cap`. */
  virtual size_t getBytes(const char* key, void* buf, size_t cap) = 0;
  virtual bool putBytes(const char* key, const void* buf, size_t len) = 0;
  virtual bool erase(const char* key) = 0;

 protected:
  // Never deleted through the interface (the updater only holds references).
  ~IOtaStore() = default;
};

/** State of an OTA slot as the bootloader tracks it (mirrors esp_ota_img_states_t). */
enum class SlotState : uint8_t { kUndefined, kNew, kPendingVerify, kValid, kInvalid, kAborted };

/** The two-slot flash layout and the bootloader's rollback bookkeeping. */
class IOtaSlots {
 public:
  virtual SlotState runningState() = 0;
  /** True when the other slot holds an image the bootloader rolled back from. */
  virtual bool hasInvalidSlot() = 0;
  /** True when this boot followed a power-on reset (supply lost and restored), not a crash or watchdog. */
  virtual bool resetWasPowerLoss() = 0;
  /** True when a previous firmware image exists that the bootloader would run. */
  virtual bool canRollBack() = 0;
  /** Mark the running image invalid and restart into the previous one. Does not return on a device. */
  virtual void rollBackAndReboot() = 0;
  virtual bool markRunningValid() = 0;
  virtual bool beginUpdate(size_t size) = 0;
  virtual size_t writeUpdate(const uint8_t* data, size_t len) = 0;
  /** Finalise and make the written image the next boot slot. */
  virtual bool endUpdate() = 0;
  virtual void abortUpdate() = 0;

 protected:
  // Never deleted through the interface (the updater only holds references).
  ~IOtaSlots() = default;
};

struct ManifestResponse {
  int status;
  char etag[65];
  size_t bodyLen;
  char body[kMaxManifestBytes + 1];
  bool tooLarge;
};

/** One HTTPS session at a time: a manifest GET, or an image stream. */
class IOtaHttp {
 public:
  /** False on transport failure. `ifNoneMatch` may be null or empty. */
  virtual bool fetchManifest(const char* path, const char* ifNoneMatch, ManifestResponse& out) = 0;
  /** Content length (>= 0), a negative HTTP status, or -1 for transport failure. */
  virtual int64_t openImage(const char* path) = 0;
  /** Bytes read, 0 at end of stream, -1 on timeout or error. */
  virtual int readImage(uint8_t* buf, size_t cap) = 0;
  virtual void closeImage() = 0;

 protected:
  // Never deleted through the interface (the updater only holds references).
  ~IOtaHttp() = default;
};

}  // namespace ota_core
