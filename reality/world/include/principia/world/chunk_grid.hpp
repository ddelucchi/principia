#pragma once

#include <principia/spacetime/coordinates.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace principia::world {

enum class ResolutionLevel {
    Dormant,
    Aggregate,
    Coarse,
    Resolved,
    HighPrecision,
};

[[nodiscard]] constexpr bool valid_resolution_level(ResolutionLevel level) noexcept
{
    switch (level) {
    case ResolutionLevel::Dormant:
    case ResolutionLevel::Aggregate:
    case ResolutionLevel::Coarse:
    case ResolutionLevel::Resolved:
    case ResolutionLevel::HighPrecision:
        return true;
    }
    return false;
}

enum class MissingNeighborPolicy {
    PreserveHalo,
    FillExplicitValue,
};

template <typename Value, std::size_t InteriorExtent = 32, std::size_t HaloWidth = 1>
class ChunkGrid2 {
public:
    static_assert(InteriorExtent > 0);
    static_assert(HaloWidth > 0);
    static_assert(HaloWidth <= InteriorExtent, "halo width cannot exceed the interior extent");
    static_assert(
        HaloWidth <= (std::numeric_limits<std::size_t>::max() - InteriorExtent) / 2,
        "chunk storage extent overflows size_t");

    static constexpr std::size_t interior_extent = InteriorExtent;
    static constexpr std::size_t halo_width = HaloWidth;
    static constexpr std::size_t storage_extent = InteriorExtent + 2 * HaloWidth;
    static_assert(
        storage_extent <= std::numeric_limits<std::size_t>::max() / storage_extent,
        "chunk storage area overflows size_t");
    static constexpr std::size_t storage_size = storage_extent * storage_extent;

    [[nodiscard]] Value& interior(std::size_t x, std::size_t y)
    {
        if (x >= InteriorExtent || y >= InteriorExtent) {
            throw std::out_of_range("interior cell is outside the chunk");
        }
        return storage_[index(x + HaloWidth, y + HaloWidth)];
    }

    [[nodiscard]] const Value& interior(std::size_t x, std::size_t y) const
    {
        if (x >= InteriorExtent || y >= InteriorExtent) {
            throw std::out_of_range("interior cell is outside the chunk");
        }
        return storage_[index(x + HaloWidth, y + HaloWidth)];
    }

    [[nodiscard]] Value& storage(std::size_t x, std::size_t y)
    {
        if (x >= storage_extent || y >= storage_extent) {
            throw std::out_of_range("storage cell is outside the chunk including halo");
        }
        return storage_[index(x, y)];
    }

    [[nodiscard]] const Value& storage(std::size_t x, std::size_t y) const
    {
        if (x >= storage_extent || y >= storage_extent) {
            throw std::out_of_range("storage cell is outside the chunk including halo");
        }
        return storage_[index(x, y)];
    }

    [[nodiscard]] const std::array<Value, storage_size>& contiguous_storage() const noexcept { return storage_; }

private:
    [[nodiscard]] static constexpr std::size_t index(std::size_t x, std::size_t y) noexcept
    {
        return y * storage_extent + x;
    }

    std::array<Value, storage_size> storage_{};
};

template <typename Value, std::size_t InteriorExtent, std::size_t HaloWidth>
void exchange_horizontal_halos(
    ChunkGrid2<Value, InteriorExtent, HaloWidth>& left,
    ChunkGrid2<Value, InteriorExtent, HaloWidth>& right)
{
    constexpr auto halo = HaloWidth;
    constexpr auto interior = InteriorExtent;
    for (std::size_t y = 0; y < interior; ++y) {
        for (std::size_t layer = 0; layer < halo; ++layer) {
            left.storage(halo + interior + layer, halo + y) = right.interior(layer, y);
            right.storage(layer, halo + y) = left.interior(interior - halo + layer, y);
        }
    }
}

template <typename Value, std::size_t InteriorExtent, std::size_t HaloWidth>
void exchange_vertical_halos(
    ChunkGrid2<Value, InteriorExtent, HaloWidth>& lower,
    ChunkGrid2<Value, InteriorExtent, HaloWidth>& upper)
{
    constexpr auto halo = HaloWidth;
    constexpr auto interior = InteriorExtent;
    for (std::size_t x = 0; x < interior; ++x) {
        for (std::size_t layer = 0; layer < halo; ++layer) {
            lower.storage(halo + x, halo + interior + layer) = upper.interior(x, layer);
            upper.storage(halo + x, layer) = lower.interior(x, interior - halo + layer);
        }
    }
}

template <typename Value, std::size_t InteriorExtent = 32, std::size_t HaloWidth = 1>
class ChunkStore2 {
public:
    using Grid = ChunkGrid2<Value, InteriorExtent, HaloWidth>;

    [[nodiscard]] bool insert(spacetime::ChunkPosition position, Grid grid = {})
    {
        return chunks_.try_emplace(position, std::move(grid)).second;
    }

    [[nodiscard]] Grid* find(spacetime::ChunkPosition position) noexcept
    {
        const auto found = chunks_.find(position);
        return found == chunks_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const std::map<spacetime::ChunkPosition, Grid>& ordered_chunks() const noexcept { return chunks_; }

private:
    std::map<spacetime::ChunkPosition, Grid> chunks_;
};

}  // namespace principia::world
