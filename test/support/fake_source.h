#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "adsb_parse/body_reader.h"

/**
 * Scripted byte source. Each call consumes the next read_sizes entry as the
 * maximum bytes to return (0 = "nothing available yet"); once the script is
 * exhausted every call returns up to max_per_read bytes. At end of data it
 * returns -1 (connection closed) when close_at_end, else 0.
 */
struct FakeSource {
  std::string data;
  std::vector<size_t> read_sizes;
  size_t max_per_read = std::numeric_limits<size_t>::max();
  bool close_at_end = true;

  size_t pos = 0;
  size_t calls = 0;
  size_t largest_request = 0;

  explicit FakeSource(std::string d) : data(std::move(d)) {}

  int read(uint8_t* buf, size_t len) {
    largest_request = std::max(largest_request, len);
    size_t limit = max_per_read;
    if (calls < read_sizes.size()) {
      limit = read_sizes[calls];
    }
    ++calls;
    if (limit == 0) {
      return 0;
    }
    if (pos >= data.size()) {
      return close_at_end ? -1 : 0;
    }
    const size_t n = std::min({limit, len, data.size() - pos});
    memcpy(buf, data.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }

  adsb_parse::ByteSource fn() {
    return [this](uint8_t* buf, size_t len) { return read(buf, len); };
  }
};

/** Millisecond clock that advances by idle_step each time the reader idles. */
struct FakeClock {
  uint32_t now = 0;
  uint32_t idle_step = 0;
  size_t idle_calls = 0;

  adsb_parse::Clock nowFn() {
    return [this] { return now; };
  }
  adsb_parse::Idle idleFn() {
    return [this] {
      ++idle_calls;
      now += idle_step;
    };
  }
};

/** Wrap body in HTTP/1.1 chunked framing with chunks of at most chunk_size bytes. */
inline std::string chunked(const std::string& body, size_t chunk_size) {
  std::string out;
  for (size_t i = 0; i < body.size(); i += chunk_size) {
    const size_t n = std::min(chunk_size, body.size() - i);
    char line[16];
    snprintf(line, sizeof(line), "%zx\r\n", n);
    out += line;
    out.append(body, i, n);
    out += "\r\n";
  }
  out += "0\r\n\r\n";
  return out;
}
