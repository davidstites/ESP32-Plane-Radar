#include <gtest/gtest.h>

#include "adsb_parse/retry_policy.h"

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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
