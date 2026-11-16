#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace adsb_parse {

/** Reads up to len bytes: returns >0 bytes read, 0 if none available yet, -1 if closed. */
using ByteSource = std::function<int(uint8_t* buf, size_t len)>;
/** Monotonic milliseconds (wraps like millis()). */
using Clock = std::function<uint32_t()>;
/** Called whenever the source has no bytes yet (e.g. to service WiFi / the portal). */
using Idle = std::function<void()>;

enum class BodyFraming { kChunked, kContentLength, kUntilClose };

enum class BodyStatus { kReading, kComplete, kTimeout, kMalformed, kClosed };

/**
 * Pull-style HTTP/1.1 response body decoder usable as an ArduinoJson custom
 * Reader. Handles chunked, Content-Length and read-until-close framing and
 * gives up once timeout_ms has elapsed since construction.
 */
class BodyReader {
 public:
  static constexpr size_t kBufferSize = 256;

  BodyReader(ByteSource source, BodyFraming framing, long content_length, Clock now_ms,
             Idle idle, uint32_t timeout_ms);

  /** Next body byte, or -1 at end of body or on error (see status()). */
  int read();
  /** Up to len body bytes; a short count means end of body or error. */
  size_t readBytes(char* buf, size_t len);
  /** Consume the rest of the body, including the final chunk and trailers. */
  bool drain();

  BodyStatus status() const { return status_; }
  /** True when the body ended cleanly and the connection can carry another request. */
  bool reusable() const;

 private:
  enum class ChunkState { kSizeLine, kExtension, kData, kDataEnd, kTrailers };

  int nextRaw();
  int nextChunked();
  bool chunkSizeDigit(int c);
  int fail(BodyStatus status);

  ByteSource source_;
  BodyFraming framing_;
  Clock now_ms_;
  Idle idle_;
  uint32_t start_ms_;
  uint32_t timeout_ms_;
  BodyStatus status_ = BodyStatus::kReading;

  uint8_t buf_[kBufferSize];
  size_t buf_pos_ = 0;
  size_t buf_len_ = 0;

  ChunkState chunk_state_ = ChunkState::kSizeLine;
  uint32_t remaining_ = 0;  // bytes left in the current chunk or Content-Length body
  int size_digits_ = 0;
  bool seen_cr_ = false;
  bool trailer_line_empty_ = true;
};

}  // namespace adsb_parse
