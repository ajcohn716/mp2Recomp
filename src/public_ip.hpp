#pragma once
#include <string>
#include <string_view>

namespace netplay {
// Returns an empty string on an invalid/non-public IPv4 response or lookup failure.
std::string parse_public_ipv4(std::string_view response);
// Blocking HTTPS lookup; call from a worker, never the SDL event thread.
std::string lookup_public_ipv4();
}
