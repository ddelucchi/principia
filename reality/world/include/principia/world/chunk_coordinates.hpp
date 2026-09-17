#pragma once

#include <principia/spacetime/coordinates.hpp>

#include <cstdint>
#include <stdexcept>

namespace principia::world {

struct DecomposedCell2 {
    spacetime::ChunkPosition chunk;
    spacetime::CellPosition local;
};

[[nodiscard]] constexpr std::int32_t floor_divide(std::int32_t value, std::int32_t divisor)
{
    if (divisor <= 0) {
        throw std::invalid_argument("chunk extent must be positive");
    }
    const auto quotient = value / divisor;
    const auto remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] constexpr DecomposedCell2 decompose_global_cell(
    spacetime::CellPosition global,
    std::int32_t chunk_extent)
{
    const auto chunk_x = floor_divide(global.x, chunk_extent);
    const auto chunk_y = floor_divide(global.y, chunk_extent);
    auto local_x = global.x % chunk_extent;
    auto local_y = global.y % chunk_extent;
    if (local_x < 0) {
        local_x += chunk_extent;
    }
    if (local_y < 0) {
        local_y += chunk_extent;
    }
    return DecomposedCell2{
        {chunk_x, chunk_y},
        {local_x, local_y},
    };
}

}  // namespace principia::world
