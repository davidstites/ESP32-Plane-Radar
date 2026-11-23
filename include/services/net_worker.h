#pragma once

namespace services::net {

/**
 * Start the single network task. It runs the ADS-B polls (and, later, the weather feeds)
 * one at a time, so only one TLS session is ever open. Call once from setup().
 */
void start();

}  // namespace services::net
