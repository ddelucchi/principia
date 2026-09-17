#pragma once

#include <principia/core/strong_id.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace principia::state {

struct StateChannelIdTag;
using StateChannelId = core::StrongId<StateChannelIdTag, std::uint32_t>;

enum class FieldLocation : std::uint8_t {
    NotSpatial,
    CellCentered,
    FaceCenteredX,
    FaceCenteredY,
    VertexCentered,
    EdgeCentered,
    ParticleCarried,
};

[[nodiscard]] constexpr bool valid_field_location(FieldLocation location) noexcept
{
    switch (location) {
    case FieldLocation::NotSpatial:
    case FieldLocation::CellCentered:
    case FieldLocation::FaceCenteredX:
    case FieldLocation::FaceCenteredY:
    case FieldLocation::VertexCentered:
    case FieldLocation::EdgeCentered:
    case FieldLocation::ParticleCarried:
        return true;
    }
    return false;
}

[[nodiscard]] inline bool has_non_whitespace(std::string_view value) noexcept
{
    return std::ranges::any_of(value, [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) == 0;
    });
}

struct ChannelDescriptor {
    StateChannelId id;
    std::string name;
    std::type_index value_type;
    FieldLocation location{FieldLocation::NotSpatial};
    bool persistent{true};
};

template <typename Value>
struct ChannelKey {
    StateChannelId id;
};

class ChannelSet {
public:
    ChannelSet() = default;
    ChannelSet(std::initializer_list<StateChannelId> channels) : channels_(channels) { normalize(); }
    explicit ChannelSet(std::vector<StateChannelId> channels) : channels_(std::move(channels)) { normalize(); }

    [[nodiscard]] bool contains(StateChannelId channel) const
    {
        return std::binary_search(channels_.begin(), channels_.end(), channel);
    }

    [[nodiscard]] bool intersects(const ChannelSet& other) const;
    [[nodiscard]] std::span<const StateChannelId> values() const noexcept { return channels_; }
    [[nodiscard]] bool empty() const noexcept { return channels_.empty(); }

    friend bool operator==(const ChannelSet&, const ChannelSet&) = default;

private:
    void normalize();
    std::vector<StateChannelId> channels_;
};

struct AccessDescriptor {
    ChannelSet reads;
    ChannelSet writes;
};

class ChannelRegistry {
public:
    template <typename Value>
    [[nodiscard]] bool register_channel(
        ChannelKey<Value> key,
        std::string name,
        FieldLocation location,
        bool persistent = true)
    {
        if (!key.id || !has_non_whitespace(name) || !valid_field_location(location)) {
            return false;
        }
        for (const auto& [registered_id, descriptor] : descriptors_) {
            if (registered_id != key.id && descriptor.name == name) {
                return false;
            }
        }
        const auto [iterator, inserted] = descriptors_.try_emplace(
            key.id,
            ChannelDescriptor{key.id, std::move(name), std::type_index(typeid(Value)), location, persistent});
        if (inserted) {
            return true;
        }
        const auto& existing = iterator->second;
        return existing.value_type == std::type_index(typeid(Value)) && existing.name == name &&
               existing.location == location && existing.persistent == persistent;
    }

    [[nodiscard]] const ChannelDescriptor* find(StateChannelId id) const noexcept;
    // A value snapshot never exposes registry storage and remains valid after
    // later registrations. Concurrent mutation still requires external locking.
    [[nodiscard]] std::vector<ChannelDescriptor> ordered_descriptors() const;

private:
    std::map<StateChannelId, ChannelDescriptor> descriptors_;
};

namespace standard_channels {

inline constexpr StateChannelId position_id{1};
inline constexpr StateChannelId momentum_id{2};
inline constexpr StateChannelId rest_mass_id{3};
inline constexpr StateChannelId mass_density_id{10};
inline constexpr StateChannelId momentum_density_id{11};
inline constexpr StateChannelId energy_density_id{12};
inline constexpr StateChannelId temperature_id{20};
inline constexpr StateChannelId internal_energy_id{21};
inline constexpr StateChannelId charge_density_id{30};
inline constexpr StateChannelId current_density_id{31};
inline constexpr StateChannelId electric_field_id{32};
inline constexpr StateChannelId magnetic_field_id{33};
inline constexpr StateChannelId stress_tensor_id{40};
inline constexpr StateChannelId strain_tensor_id{41};
inline constexpr StateChannelId metric_field_id{50};
inline constexpr StateChannelId effective_gravity_id{60};

}  // namespace standard_channels

[[nodiscard]] ChannelRegistry make_standard_channel_registry();

}  // namespace principia::state
