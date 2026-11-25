#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ota_core/sha256.h"

using ota_core::Sha256;

namespace {

std::string hex(const uint8_t* d) {
  std::string s;
  char b[3];
  for (int i = 0; i < 32; ++i) {
    std::snprintf(b, sizeof(b), "%02x", d[i]);
    s += b;
  }
  return s;
}

std::string digestOf(const std::string& in) {
  Sha256 h;
  h.start();
  h.update(reinterpret_cast<const uint8_t*>(in.data()), in.size());
  uint8_t out[32];
  h.finish(out);
  return hex(out);
}

}  // namespace

TEST(Sha256, EmptyInput) {
  EXPECT_EQ(digestOf(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, Abc) {
  EXPECT_EQ(digestOf("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, TwoBlockMessage) {
  EXPECT_EQ(digestOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, MillionAs) {
  EXPECT_EQ(digestOf(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, StreamingSplitsMatchOneShot) {
  std::vector<uint8_t> data(130);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7);
  Sha256 whole;
  whole.start();
  whole.update(data.data(), data.size());
  uint8_t want[32];
  whole.finish(want);

  for (size_t split = 0; split <= data.size(); ++split) {
    Sha256 h;
    h.start();
    h.update(data.data(), split);
    h.update(data.data() + split, data.size() - split);
    uint8_t got[32];
    h.finish(got);
    EXPECT_EQ(hex(got), hex(want)) << "split at " << split;
  }
}

TEST(Sha256, ReuseAfterFinishNeedsStart) {
  Sha256 h;
  h.start();
  h.update(reinterpret_cast<const uint8_t*>("abc"), 3);
  uint8_t first[32];
  h.finish(first);

  h.start();  // a fresh start must discard everything from the first hash
  uint8_t second[32];
  h.finish(second);
  EXPECT_EQ(hex(second), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
