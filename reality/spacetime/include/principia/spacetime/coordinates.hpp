#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/math/vector.hpp>
#include <principia/spacetime/time.hpp>
#include <principia/units/quantity.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>

namespace principia::spacetime {

struct FrameIdTag;
using FrameId = core::StrongId<FrameIdTag, std::uint32_t>;

struct CoordinateDimensions {
    std::size_t world_topology_dimension{2};
    std::size_t physical_field_dimension{3};
    std::size_t spacetime_model_dimension{4};
};

template <std::size_t Dimension>
using WorldPosition = math::Vector<Dimension, units::Length>;

template <std::size_t Dimension>
using LocalPosition = math::Vector<Dimension, units::Length>;

struct ChunkPosition {
    std::int32_t x{};
    std::int32_t y{};
    friend constexpr auto operator<=>(const ChunkPosition&, const ChunkPosition&) = default;
};

struct CellPosition {
    std::int32_t x{};
    std::int32_t y{};
    friend constexpr auto operator<=>(const CellPosition&, const CellPosition&) = default;
};

template <std::size_t Dimension>
struct Event {
    SimulationTime time{};
    WorldPosition<Dimension> position{};
    FrameId frame{};
};

}  // namespace principia::spacetime

