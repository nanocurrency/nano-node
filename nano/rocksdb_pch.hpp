#pragma once

// Precompiled header for the bundled RocksDB static library.
//
// A vcperf /jsonAnalysis trace of a clean nano_node build showed RocksDB's own
// headers dominate frontend parse cost — 9 of the 15 hottest included files are
// RocksDB internals. Its backbone headers are parsed by the large majority of
// RocksDB's ~700 translation units: rocksdb/options.h (247 TUs),
// rocksdb/listener.h (251 TUs) and port/win/port_win.h (261 TUs, via port.h),
// plus heavy STL. RocksDB ships with no PCH, so precompiling that backbone once
// amortizes the parse across every RocksDB TU. This is the single largest
// untapped lever in the build.

// --- Tier 1: standard library ---
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// --- RocksDB backbone (resolved via the rocksdb target's include dirs) ---
// port/port.h expands to port/win/port_win.h on Windows.
#include "port/port.h"
#include "rocksdb/options.h"
#include "rocksdb/listener.h"
