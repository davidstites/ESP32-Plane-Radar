#include "adsb_parse/body_reader.h"

#include <utility>

namespace adsb_parse {

namespace {

constexpr int kEnd = -1;
constexpr int kMaxChunkSizeDigits = 8;

int hexValue(int c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}  // namespace

BodyReader::BodyReader(ByteSource source, BodyFraming framing, long content_length,
                       Clock now_ms, Idle idle, uint32_t timeout_ms)
    : source_(std::move(source)),
      framing_(framing),
      now_ms_(std::move(now_ms)),
      idle_(std::move(idle)),
      start_ms_(now_ms_()),
      timeout_ms_(timeout_ms) {
  if (framing_ == BodyFraming::kContentLength) {
    remaining_ = content_length > 0 ? static_cast<uint32_t>(content_length) : 0;
    if (remaining_ == 0) {
      status_ = BodyStatus::kComplete;
    }
  }
}

int BodyReader::fail(BodyStatus status) {
  status_ = status;
  return kEnd;
}

int BodyReader::nextRaw() {
  while (buf_pos_ >= buf_len_) {
    if (static_cast<uint32_t>(now_ms_() - start_ms_) >= timeout_ms_) {
      return fail(BodyStatus::kTimeout);
    }
    size_t want = kBufferSize;
    if (framing_ == BodyFraming::kContentLength && remaining_ < want) {
      want = remaining_;  // never consume bytes past the body
    }
    const int n = source_(buf_, want);
    if (n < 0) {
      return fail(framing_ == BodyFraming::kUntilClose ? BodyStatus::kComplete
                                                       : BodyStatus::kClosed);
    }
    if (n == 0) {
      idle_();
      continue;
    }
    buf_pos_ = 0;
    buf_len_ = static_cast<size_t>(n);
  }
  return buf_[buf_pos_++];
}

bool BodyReader::chunkSizeDigit(int c) {
  const int v = hexValue(c);
  if (v < 0 || ++size_digits_ > kMaxChunkSizeDigits) {
    return false;
  }
  remaining_ = (remaining_ << 4) | static_cast<uint32_t>(v);
  return true;
}

int BodyReader::nextChunked() {
  for (;;) {
    const int c = nextRaw();
    if (c == kEnd) {
      return kEnd;
    }
    switch (chunk_state_) {
      case ChunkState::kSizeLine:
      case ChunkState::kExtension:
        if (c == '\n') {
          if (size_digits_ == 0) {
            return fail(BodyStatus::kMalformed);
          }
          size_digits_ = 0;
          chunk_state_ = remaining_ == 0 ? ChunkState::kTrailers : ChunkState::kData;
        } else if (c == ';' || c == '\r') {
          chunk_state_ = ChunkState::kExtension;
        } else if (chunk_state_ == ChunkState::kSizeLine && !chunkSizeDigit(c)) {
          return fail(BodyStatus::kMalformed);
        }
        break;
      case ChunkState::kData:
        if (--remaining_ == 0) {
          chunk_state_ = ChunkState::kDataEnd;
          seen_cr_ = false;
        }
        return c;
      case ChunkState::kDataEnd:
        if (c == '\n') {
          chunk_state_ = ChunkState::kSizeLine;
        } else if (c == '\r' && !seen_cr_) {
          seen_cr_ = true;
        } else {
          return fail(BodyStatus::kMalformed);
        }
        break;
      case ChunkState::kTrailers:
        if (c == '\n') {
          if (trailer_line_empty_) {
            return fail(BodyStatus::kComplete);
          }
          trailer_line_empty_ = true;
        } else if (c != '\r') {
          trailer_line_empty_ = false;
        }
        break;
    }
  }
}

int BodyReader::read() {
  if (status_ != BodyStatus::kReading) {
    return kEnd;
  }
  if (framing_ == BodyFraming::kChunked) {
    return nextChunked();
  }
  const int c = nextRaw();
  if (c != kEnd && framing_ == BodyFraming::kContentLength && --remaining_ == 0) {
    status_ = BodyStatus::kComplete;
  }
  return c;
}

size_t BodyReader::readBytes(char* buf, size_t len) {
  size_t n = 0;
  while (n < len) {
    const int c = read();
    if (c == kEnd) {
      break;
    }
    buf[n++] = static_cast<char>(c);
  }
  return n;
}

bool BodyReader::drain() {
  while (read() != kEnd) {
  }
  return status_ == BodyStatus::kComplete;
}

bool BodyReader::reusable() const {
  return status_ == BodyStatus::kComplete && framing_ != BodyFraming::kUntilClose;
}

}  // namespace adsb_parse
