#pragma once

#include <principia/core/strong_id.hpp>

#include <cstdint>

namespace principia::core {

struct SchemaVersionTag;
using SchemaVersion = StrongId<SchemaVersionTag, std::uint32_t>;

}  // namespace principia::core

