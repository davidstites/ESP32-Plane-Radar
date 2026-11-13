#pragma once

#include <fstream>
#include <sstream>
#include <string>

/** Read test/fixtures/<name> into a string; empty if missing. */
inline std::string readFixture(const char* name) {
  std::ifstream in(std::string(ADSB_FIXTURE_DIR) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
