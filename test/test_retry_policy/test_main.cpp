#include <gtest/gtest.h>

#include "adsb_parse/retry_policy.h"

using adsb_parse::shouldKeepTryingToConnect;
using adsb_parse::shouldRetryOnFreshConnection;

TEST(RetryPolicy, RetriesTransportErrorOnReusedConnection) {
  EXPECT_TRUE(shouldRetryOnFreshConnection(-1, true));   // connection refused
  EXPECT_TRUE(shouldRetryOnFreshConnection(-4, true));   // not connected
  EXPECT_TRUE(shouldRetryOnFreshConnection(-11, true));  // read timeout
  EXPECT_TRUE(shouldRetryOnFreshConnection(0, true));
}

TEST(RetryPolicy, NoRetryOnFreshConnection) {
  EXPECT_FALSE(shouldRetryOnFreshConnection(-1, false));
  EXPECT_FALSE(shouldRetryOnFreshConnection(-11, false));
}

TEST(RetryPolicy, NoRetryOnHttpStatus) {
  EXPECT_FALSE(shouldRetryOnFreshConnection(200, true));
  EXPECT_FALSE(shouldRetryOnFreshConnection(429, true));
  EXPECT_FALSE(shouldRetryOnFreshConnection(500, true));
}

TEST(RetryPolicy, KeepsTryingWhileTcpCannotConnect) {
  EXPECT_TRUE(shouldKeepTryingToConnect(-1, 0));   // refused, no TLS attempt yet
  EXPECT_TRUE(shouldKeepTryingToConnect(-1, -1));  // refused, socket/TCP connect failed
  EXPECT_TRUE(shouldKeepTryingToConnect(-4, -1));  // not connected
}

TEST(RetryPolicy, StopsOnTlsHandshakeOrVerificationFailure) {
  EXPECT_FALSE(shouldKeepTryingToConnect(-1, -9984));   // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
  EXPECT_FALSE(shouldKeepTryingToConnect(-1, -0x7780)); // MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE
}

TEST(RetryPolicy, StopsOnOtherHttpResults) {
  EXPECT_FALSE(shouldKeepTryingToConnect(200, 0));
  EXPECT_FALSE(shouldKeepTryingToConnect(-11, 0));  // read timeout
  EXPECT_FALSE(shouldKeepTryingToConnect(-2, -1));  // send header failed
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
