#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/state/channel.hpp>
#include <principia/units/quantity.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace principia::boundaries {

struct BoundaryIdTag;
using BoundaryId = core::StrongId<BoundaryIdTag, std::uint64_t>;

enum class BoundaryConditionKind : std::uint8_t {
    MechanicallyRigid,
    ThermallyConductive,
    ThermallyInsulating,
    ElectricallyConductive,
    ElectricallyInsulating,
    OpticallyTransparent,
    FluidImpermeable,
};

struct AxisAlignedBox2 {
    spacetime::WorldPosition<2> minimum;
    spacetime::WorldPosition<2> maximum;
};

struct BoundaryDefinition {
    BoundaryId id;
    std::string name;
    AxisAlignedBox2 extent;
    std::map<state::StateChannelId, BoundaryConditionKind> conditions;
};

enum class BoundaryRegistryError : std::uint8_t {
    InvalidBoundaryId,
    DuplicateBoundaryId,
    MissingBoundary,
    MismatchedBoundaryId,
    EmptyName,
    NonFiniteExtent,
    InvertedExtent,
    InvalidChannelId,
    InvalidCondition,
    IncompatibleChannelCondition,
    InvalidRevision,
    RevisionExhausted,
};

using BoundaryRegistryRevision = std::uint64_t;
using BoundaryMutationResult = std::expected<BoundaryRegistryRevision, BoundaryRegistryError>;

[[nodiscard]] constexpr bool valid_boundary_condition(BoundaryConditionKind condition) noexcept
{
    switch (condition) {
    case BoundaryConditionKind::MechanicallyRigid:
    case BoundaryConditionKind::ThermallyConductive:
    case BoundaryConditionKind::ThermallyInsulating:
    case BoundaryConditionKind::ElectricallyConductive:
    case BoundaryConditionKind::ElectricallyInsulating:
    case BoundaryConditionKind::OpticallyTransparent:
    case BoundaryConditionKind::FluidImpermeable:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool boundary_condition_compatible(
    state::StateChannelId channel,
    BoundaryConditionKind condition) noexcept
{
    using namespace state::standard_channels;
    switch (condition) {
    case BoundaryConditionKind::MechanicallyRigid:
        return channel == position_id || channel == momentum_id || channel == momentum_density_id ||
               channel == stress_tensor_id;
    case BoundaryConditionKind::ThermallyConductive:
    case BoundaryConditionKind::ThermallyInsulating:
        return channel == temperature_id || channel == internal_energy_id || channel == energy_density_id;
    case BoundaryConditionKind::ElectricallyConductive:
    case BoundaryConditionKind::ElectricallyInsulating:
        return channel == charge_density_id || channel == current_density_id || channel == electric_field_id;
    case BoundaryConditionKind::OpticallyTransparent:
        // No optical propagation channel exists in the foundation schema yet.
        return false;
    case BoundaryConditionKind::FluidImpermeable:
        return channel == mass_density_id || channel == momentum_density_id;
    }
    return false;
}

[[nodiscard]] inline std::expected<void, BoundaryRegistryError> validate_boundary_definition(
    const BoundaryDefinition& definition)
{
    if (!definition.id) {
        return std::unexpected(BoundaryRegistryError::InvalidBoundaryId);
    }
    if (!state::has_non_whitespace(definition.name)) {
        return std::unexpected(BoundaryRegistryError::EmptyName);
    }

    const auto minimum_x = units::in_metres(definition.extent.minimum[0]);
    const auto minimum_y = units::in_metres(definition.extent.minimum[1]);
    const auto maximum_x = units::in_metres(definition.extent.maximum[0]);
    const auto maximum_y = units::in_metres(definition.extent.maximum[1]);
    if (!std::isfinite(minimum_x) || !std::isfinite(minimum_y) || !std::isfinite(maximum_x) ||
        !std::isfinite(maximum_y)) {
        return std::unexpected(BoundaryRegistryError::NonFiniteExtent);
    }
    if (minimum_x > maximum_x || minimum_y > maximum_y) {
        return std::unexpected(BoundaryRegistryError::InvertedExtent);
    }

    for (const auto& [channel, condition] : definition.conditions) {
        if (!channel) {
            return std::unexpected(BoundaryRegistryError::InvalidChannelId);
        }
        if (!valid_boundary_condition(condition)) {
            return std::unexpected(BoundaryRegistryError::InvalidCondition);
        }
        if (!boundary_condition_compatible(channel, condition)) {
            return std::unexpected(BoundaryRegistryError::IncompatibleChannelCondition);
        }
    }
    return {};
}

class BoundaryRegistry {
public:
    // Persistence-only transactional import. All definitions are validated in
    // isolation and the live registry is left untouched on any failure.
    [[nodiscard]] std::expected<void, BoundaryRegistryError> restore(
        BoundaryRegistryRevision revision,
        std::vector<BoundaryDefinition> definitions)
    {
        if (revision == std::numeric_limits<BoundaryRegistryRevision>::max()) {
            return std::unexpected(BoundaryRegistryError::RevisionExhausted);
        }
        if (revision < definitions.size()) {
            return std::unexpected(BoundaryRegistryError::InvalidRevision);
        }

        std::map<BoundaryId, BoundaryDefinition> restored;
        for (auto& definition : definitions) {
            const auto validity = validate_boundary_definition(definition);
            if (!validity) {
                return std::unexpected(validity.error());
            }
            const auto id = definition.id;
            if (!restored.try_emplace(id, std::move(definition)).second) {
                return std::unexpected(BoundaryRegistryError::DuplicateBoundaryId);
            }
        }

        definitions_ = std::move(restored);
        revision_ = revision;
        return {};
    }

    [[nodiscard]] BoundaryMutationResult try_add(BoundaryDefinition definition)
    {
        const auto validity = validate_boundary_definition(definition);
        if (!validity) {
            return std::unexpected(validity.error());
        }
        if (definitions_.contains(definition.id)) {
            return std::unexpected(BoundaryRegistryError::DuplicateBoundaryId);
        }
        if (revision_ == std::numeric_limits<BoundaryRegistryRevision>::max()) {
            return std::unexpected(BoundaryRegistryError::RevisionExhausted);
        }

        const auto id = definition.id;
        const auto [position, inserted] = definitions_.try_emplace(id, std::move(definition));
        static_cast<void>(position);
        if (!inserted) {
            return std::unexpected(BoundaryRegistryError::DuplicateBoundaryId);
        }
        ++revision_;
        return revision_;
    }

    [[nodiscard]] bool add(BoundaryDefinition definition)
    {
        return try_add(std::move(definition)).has_value();
    }

    [[nodiscard]] BoundaryMutationResult try_emplace(BoundaryId key, BoundaryDefinition definition)
    {
        if (key != definition.id) {
            return std::unexpected(BoundaryRegistryError::MismatchedBoundaryId);
        }
        return try_add(std::move(definition));
    }

    [[nodiscard]] bool emplace(BoundaryId key, BoundaryDefinition definition)
    {
        return try_emplace(key, std::move(definition)).has_value();
    }

    [[nodiscard]] BoundaryMutationResult try_replace(BoundaryDefinition definition)
    {
        const auto validity = validate_boundary_definition(definition);
        if (!validity) {
            return std::unexpected(validity.error());
        }
        const auto found = definitions_.find(definition.id);
        if (found == definitions_.end()) {
            return std::unexpected(BoundaryRegistryError::MissingBoundary);
        }
        if (revision_ == std::numeric_limits<BoundaryRegistryRevision>::max()) {
            return std::unexpected(BoundaryRegistryError::RevisionExhausted);
        }
        found->second = std::move(definition);
        ++revision_;
        return revision_;
    }

    [[nodiscard]] bool replace(BoundaryDefinition definition)
    {
        return try_replace(std::move(definition)).has_value();
    }

    [[nodiscard]] BoundaryMutationResult try_remove(BoundaryId id)
    {
        if (!id) {
            return std::unexpected(BoundaryRegistryError::InvalidBoundaryId);
        }
        const auto found = definitions_.find(id);
        if (found == definitions_.end()) {
            return std::unexpected(BoundaryRegistryError::MissingBoundary);
        }
        if (revision_ == std::numeric_limits<BoundaryRegistryRevision>::max()) {
            return std::unexpected(BoundaryRegistryError::RevisionExhausted);
        }
        definitions_.erase(found);
        ++revision_;
        return revision_;
    }

    [[nodiscard]] bool remove(BoundaryId id) { return try_remove(id).has_value(); }

    [[nodiscard]] const BoundaryDefinition* find(BoundaryId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] bool contains(BoundaryId id) const noexcept { return definitions_.contains(id); }

    [[nodiscard]] const BoundaryDefinition& at(BoundaryId id) const { return definitions_.at(id); }

    [[nodiscard]] const std::map<BoundaryId, BoundaryDefinition>& ordered_definitions() const noexcept
    {
        return definitions_;
    }

    [[nodiscard]] auto begin() const noexcept { return definitions_.cbegin(); }
    [[nodiscard]] auto end() const noexcept { return definitions_.cend(); }
    [[nodiscard]] bool empty() const noexcept { return definitions_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }
    [[nodiscard]] BoundaryRegistryRevision revision() const noexcept { return revision_; }

private:
    std::map<BoundaryId, BoundaryDefinition> definitions_;
    BoundaryRegistryRevision revision_{};
};

}  // namespace principia::boundaries
