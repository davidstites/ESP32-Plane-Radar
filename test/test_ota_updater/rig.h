// Shared fixture for the updater tests: a rig of fakes wired to a golden signed manifest.
#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ota_core/updater.h"
#include "support/ota_fakes.h"
#include "support/ota_vectors.h"
#include "support/tweetnacl_verify.h"

namespace rig {

using namespace ota_core;
using namespace ota_fakes;
namespace v = ota_vectors;


const char kManifestPath[] = "/planeradar/manifest.json";
const char kImage[] = "planeradar test firmware image";  // sha256 is v::kSha
const uint8_t* const kOneKey[] = {v::kPub};
const uint8_t* const kOtherKey[] = {v::kOtherPub};

struct Options {
  std::string sha = v::kShaHex;
  std::string signature = v::kSigProd;
  std::string channel = "prod";
  std::string firmware = "/planeradar/firmware-8.bin";
  std::string build = "8";
  std::string rollout;     // empty: omitted
  std::string candidates;  // a JSON array, empty: omitted
};

inline std::string body(const Options& o = Options()) {
  std::string s = "{\"build\":" + o.build + ",\"min_build\":5,\"sha256\":\"" + o.sha + "\",\"channel\":\"" +
                  o.channel + "\",\"signature\":\"" + o.signature + "\",\"firmware\":\"" + o.firmware + "\"";
  if (!o.rollout.empty()) s += ",\"rollout\":" + o.rollout;
  if (!o.candidates.empty()) s += ",\"candidates\":" + o.candidates;
  return s + "}";
}

struct Rig {
  FakeStore store;
  FakeSlots slots;
  FakeHttp http;
  Log log;
  Config cfg{};
  Rig() {
    store.log = &log;
    slots.log = &log;
    cfg.basePath = "/planeradar";
    memcpy(cfg.deviceId, v::kDeviceId, sizeof(cfg.deviceId));
    cfg.runningBuild = 6;
    cfg.keys = KeyList{kOneKey, 1};
    cfg.verify = tweetnaclEd25519Verify;
    serve(body());
    http.image = kImage;
    http.openResult = static_cast<int64_t>(http.image.size());
  }
  void serve(const std::string& manifest, int status = 200, const std::string& etag = "\"v8\"") {
    FakeHttp::Manifest m;
    m.status = status;
    m.etag = etag;
    m.body = manifest;
    http.manifests[kManifestPath] = m;
  }
  CheckResult run() {
    Updater u(cfg, store, slots, http);
    return u.check();
  }
  std::string storedEtag() {
    char buf[80];
    OtaState(store).etag(buf, sizeof(buf));
    return buf;
  }
  size_t indexOf(const std::string& entry) const {
    return static_cast<size_t>(std::find(log.begin(), log.end(), entry) - log.begin());
  }
  bool logged(const std::string& entry) const { return indexOf(entry) < log.size(); }
};

inline void expectAbortedWithoutActivation(Rig& r) {
  EXPECT_EQ(r.slots.endCalls, 0);
  EXPECT_TRUE(r.logged("abortUpdate"));
  EXPECT_EQ(r.store.uints.count("pending"), 0u);
  EXPECT_EQ(r.storedEtag(), "");
  EXPECT_TRUE(r.http.closed);
}


}  // namespace rig
