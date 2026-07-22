#pragma once

// Precompiled header for nano's first-party C++ libraries.
//
// Contents are derived from a vcperf /jsonAnalysis trace of a clean nano_node
// build: <chrono> was the #3 hottest included header (parsed in 272 TUs), the
// most-included nano header (logging.hpp, 66 TUs) is a thin wrapper over
// spdlog/fmt, boost.asio ip/tcp.hpp was hot (71 TUs), and nano's core number
// types (blocks.hpp, 62 TUs) pull in boost.multiprecision. Precompiling those
// stable STL / third-party leaves once amortizes the parse across all nano TUs.
// Only skill Tier-1 (STL) and Tier-2 (third-party) headers are included here;
// volatile nano project headers are deliberately excluded.

// --- Tier 1: standard library (never changes) ---
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// --- Tier 2: third-party (changes only on dependency upgrade) ---
#include <boost/asio.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>
