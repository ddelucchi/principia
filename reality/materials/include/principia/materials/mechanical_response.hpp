#pragma once

#include <principia/materials/material.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace principia::materials {

enum class ImpactResponseKind : std::uint8_t {
    FrictionlessRestitution,
};

struct MechanicalResponseDefinition {
    MechanicalModelId id;
    std::string name;
    ImpactResponseKind impact_kind{ImpactResponseKind::FrictionlessRestitution};
    double normal_coefficient_of_restitution{1.0};

    friend bool operator==(const MechanicalResponseDefinition&, const MechanicalResponseDefinition&) = default;
};

enum class MechanicalResponseError : std::uint8_t {
    InvalidIdentifier,
    EmptyName,
    InvalidImpactKind,
    NonFiniteCoefficient,
    CoefficientOutsideUnitInterval,
    DuplicateIdentifier,
    InvalidRevision,
    RevisionExhausted,
};

[[nodiscard]] inline std::expected<void, MechanicalResponseError> validate_mechanical_response(
    const MechanicalResponseDefinition& definition)
{
    if (!definition.id) {
        return std::unexpected(MechanicalResponseError::InvalidIdentifier);
    }
    if (definition.name.empty() ||
        !std::ranges::any_of(definition.name, [](unsigned char character) {
            return std::isspace(character) == 0;
        })) {
        return std::unexpected(MechanicalResponseError::EmptyName);
    }
    if (definition.impact_kind != ImpactResponseKind::FrictionlessRestitution) {
        return std::unexpected(MechanicalResponseError::InvalidImpactKind);
    }
    if (!std::isfinite(definition.normal_coefficient_of_restitution)) {
        return std::unexpected(MechanicalResponseError::NonFiniteCoefficient);
    }
    if (definition.normal_coefficient_of_restitution < 0.0 ||
        definition.normal_coefficient_of_restitution > 1.0) {
        return std::unexpected(MechanicalResponseError::CoefficientOutsideUnitInterval);
    }
    return {};
}

class MechanicalResponseRegistry {
public:
    [[nodiscard]] std::expected<std::uint64_t, MechanicalResponseError> try_add(
        MechanicalResponseDefinition definition)
    {
        const auto validity = validate_mechanical_response(definition);
        if (!validity) {
            return std::unexpected(validity.error());
        }
        if (definitions_.contains(definition.id)) {
            return std::unexpected(MechanicalResponseError::DuplicateIdentifier);
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(MechanicalResponseError::RevisionExhausted);
        }
        const auto id = definition.id;
        definitions_.emplace(id, std::move(definition));
        ++revision_;
        return revision_;
    }

    // Persistence-only atomic restore of already-versioned canonical metadata.
    [[nodiscard]] std::expected<void, MechanicalResponseError> restore(
        std::uint64_t revision,
        std::vector<MechanicalResponseDefinition> definitions)
    {
        // The canonical registry currently has only successful add mutations,
        // so its reachable revision is exactly the number of live definitions.
        if (revision == std::numeric_limits<std::uint64_t>::max() || revision != definitions.size()) {
            return std::unexpected(MechanicalResponseError::InvalidRevision);
        }
        std::map<MechanicalModelId, MechanicalResponseDefinition> restored;
        for (auto& definition : definitions) {
            const auto validity = validate_mechanical_response(definition);
            if (!validity) {
                return std::unexpected(validity.error());
            }
            const auto id = definition.id;
            if (!restored.emplace(id, std::move(definition)).second) {
                return std::unexpected(MechanicalResponseError::DuplicateIdentifier);
            }
        }
        definitions_ = std::move(restored);
        revision_ = revision;
        return {};
    }

    [[nodiscard]] const MechanicalResponseDefinition* find(MechanicalModelId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const std::map<MechanicalModelId, MechanicalResponseDefinition>& ordered_definitions() const noexcept
    {
        return definitions_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::map<MechanicalModelId, MechanicalResponseDefinition> definitions_;
    std::uint64_t revision_{};
};

[[nodiscard]] inline double effective_restitution(
    const MechanicalResponseDefinition& left,
    const MechanicalResponseDefinition& right) noexcept
{
    // The foundation's explicit pair law is the more dissipative response.
    // Replacing this composition policy is a mechanical-model contract change.
    return std::min(
        left.normal_coefficient_of_restitution,
        right.normal_coefficient_of_restitution);
}

}  // namespace principia::materials
