#include <gtest/gtest.h>

#include "adsb_parse/connection_policy.h"

using adsb_parse::BodyFraming;
using adsb_parse::errorBodyAction;
using adsb_parse::ErrorBodyAction;
using adsb_parse::headerTimeoutMs;
using adsb_parse::kRequestTimeoutMs;
using adsb_parse::kReusedHeaderTimeoutMs;
using adsb_parse::socketClosed;

TEST(ConnectionPolicy, ReusedConnectionGetsShortHeaderTimeout) {
  EXPECT_EQ(headerTimeoutMs(true), kReusedHeaderTimeoutMs);
  EXPECT_EQ(headerTimeoutMs(false), kRequestTimeoutMs);
  EXPECT_LT(kReusedHeaderTimeoutMs, kRequestTimeoutMs);
}

TEST(ConnectionPolicy, PeekOfZeroMeansPeerClosed) {
  EXPECT_TRUE(socketClosed(true, 0, false));
}

TEST(ConnectionPolicy, WouldBlockMeansStillOpen) {
  EXPECT_FALSE(socketClosed(true, -1, true));
}

TEST(ConnectionPolicy, PendingEncryptedBytesMeanStillOpen) {
  EXPECT_FALSE(socketClosed(true, 1, false));
}

TEST(ConnectionPolicy, SocketErrorMeansClosed) {
  EXPECT_TRUE(socketClosed(true, -1, false));  // e.g. ECONNRESET
}

TEST(ConnectionPolicy, TlsLayerDisconnectedMeansClosed) {
  EXPECT_TRUE(socketClosed(false, -1, true));
  EXPECT_TRUE(socketClosed(false, 1, false));
}

TEST(ConnectionPolicy, ErrorBodyWithFramingIsDrained) {
  EXPECT_EQ(errorBodyAction(429, BodyFraming::kChunked), ErrorBodyAction::kDrain);
  EXPECT_EQ(errorBodyAction(503, BodyFraming::kContentLength), ErrorBodyAction::kDrain);
  EXPECT_EQ(errorBodyAction(404, BodyFraming::kContentLength), ErrorBodyAction::kDrain);
}

TEST(ConnectionPolicy, ErrorBodyUntilCloseIsClosedNotDrained) {
  // No length: the body only ends when the server closes, so draining can't keep the connection.
  EXPECT_EQ(errorBodyAction(500, BodyFraming::kUntilClose), ErrorBodyAction::kClose);
}

TEST(ConnectionPolicy, BodilessStatusesKeepConnection) {
  EXPECT_EQ(errorBodyAction(204, BodyFraming::kUntilClose), ErrorBodyAction::kKeep);
  EXPECT_EQ(errorBodyAction(304, BodyFraming::kUntilClose), ErrorBodyAction::kKeep);
  EXPECT_EQ(errorBodyAction(100, BodyFraming::kUntilClose), ErrorBodyAction::kKeep);
}

TEST(ConnectionPolicy, TransportErrorClosesConnection) {
  EXPECT_EQ(errorBodyAction(-11, BodyFraming::kChunked), ErrorBodyAction::kClose);
  EXPECT_EQ(errorBodyAction(0, BodyFraming::kChunked), ErrorBodyAction::kClose);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
