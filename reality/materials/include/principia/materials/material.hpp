#pragma once

#include <principia/core/strong_id.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace principia::materials {

struct MaterialIdTag;
struct CompositionIdTag;
struct MechanicalModelIdTag;
struct ThermalModelIdTag;
struct ElectricalModelIdTag;
struct MagneticModelIdTag;
struct OpticalModelIdTag;
struct PhaseModelIdTag;

using MaterialId = core::StrongId<MaterialIdTag, std::uint32_t>;
using CompositionId = core::StrongId<CompositionIdTag, std::uint32_t>;
using MechanicalModelId = core::StrongId<MechanicalModelIdTag, std::uint32_t>;
using ThermalModelId = core::StrongId<ThermalModelIdTag, std::uint32_t>;
using ElectricalModelId = core::StrongId<ElectricalModelIdTag, std::uint32_t>;
using MagneticModelId = core::StrongId<MagneticModelIdTag, std::uint32_t>;
using OpticalModelId = core::StrongId<OpticalModelIdTag, std::uint32_t>;
using PhaseModelId = core::StrongId<PhaseModelIdTag, std::uint32_t>;

struct MaterialDefinition {
    MaterialId id;
    std::string name;
    CompositionId composition;
    MechanicalModelId mechanical;
    ThermalModelId thermal;
    ElectricalModelId electrical;
    MagneticModelId magnetic;
    OpticalModelId optical;
    PhaseModelId phase;
};

// Catalog families are optional because most model registries are deliberately
// deferred. Each supplied family turns its nonzero model reference into a
// checked foreign key without pretending the other model families are known.
struct MaterialModelCatalog {
    std::optional<std::set<CompositionId>> compositions;
    std::optional<std::set<MechanicalModelId>> mechanical_models;
    std::optional<std::set<ThermalModelId>> thermal_models;
    std::optional<std::set<ElectricalModelId>> electrical_models;
    std::optional<std::set<MagneticModelId>> magnetic_models;
    std::optional<std::set<OpticalModelId>> optical_models;
    std::optional<std::set<PhaseModelId>> phase_models;
};

enum class MaterialRegistryError : std::uint8_t {
    InvalidMaterialId,
    DuplicateMaterialId,
    EmptyName,
    InvalidCompositionId,
    InvalidMechanicalModelId,
    InvalidThermalModelId,
    InvalidElectricalModelId,
    InvalidMagneticModelId,
    InvalidOpticalModelId,
    InvalidPhaseModelId,
    UnresolvedComposition,
    UnresolvedMechanicalModel,
    UnresolvedThermalModel,
    UnresolvedElectricalModel,
    UnresolvedMagneticModel,
    UnresolvedOpticalModel,
    UnresolvedPhaseModel,
    InvalidRevision,
    RevisionExhausted,
};

using MaterialRegistryRevision = std::uint64_t;
using MaterialMutationResult = std::expected<MaterialRegistryRevision, MaterialRegistryError>;

[[nodiscard]] inline std::expected<void, MaterialRegistryError> validate_material_model_catalog(
    const MaterialModelCatalog& catalog)
{
    const auto contains_reserved_id = []<typename Id>(const std::optional<std::set<Id>>& ids) {
        return ids && std::ranges::any_of(*ids, [](Id id) { return !id; });
    };
    if (contains_reserved_id(catalog.compositions)) {
        return std::unexpected(MaterialRegistryError::InvalidCompositionId);
    }
    if (contains_reserved_id(catalog.mechanical_models)) {
        return std::unexpected(MaterialRegistryError::InvalidMechanicalModelId);
    }
    if (contains_reserved_id(catalog.thermal_models)) {
        return std::unexpected(MaterialRegistryError::InvalidThermalModelId);
    }
    if (contains_reserved_id(catalog.electrical_models)) {
        return std::unexpected(MaterialRegistryError::InvalidElectricalModelId);
    }
    if (contains_reserved_id(catalog.magnetic_models)) {
        return std::unexpected(MaterialRegistryError::InvalidMagneticModelId);
    }
    if (contains_reserved_id(catalog.optical_models)) {
        return std::unexpected(MaterialRegistryError::InvalidOpticalModelId);
    }
    if (contains_reserved_id(catalog.phase_models)) {
        return std::unexpected(MaterialRegistryError::InvalidPhaseModelId);
    }
    return {};
}

[[nodiscard]] inline std::expected<void, MaterialRegistryError> validate_material_definition(
    const MaterialDefinition& definition,
    const MaterialModelCatalog* catalog = nullptr)
{
    if (!definition.id) {
        return std::unexpected(MaterialRegistryError::InvalidMaterialId);
    }
    if (definition.name.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
        return std::unexpected(MaterialRegistryError::EmptyName);
    }
    if (!definition.composition) {
        return std::unexpected(MaterialRegistryError::InvalidCompositionId);
    }
    if (!definition.mechanical) {
        return std::unexpected(MaterialRegistryError::InvalidMechanicalModelId);
    }
    if (!definition.thermal) {
        return std::unexpected(MaterialRegistryError::InvalidThermalModelId);
    }
    if (!definition.electrical) {
        return std::unexpected(MaterialRegistryError::InvalidElectricalModelId);
    }
    if (!definition.magnetic) {
        return std::unexpected(MaterialRegistryError::InvalidMagneticModelId);
    }
    if (!definition.optical) {
        return std::unexpected(MaterialRegistryError::InvalidOpticalModelId);
    }
    if (!definition.phase) {
        return std::unexpected(MaterialRegistryError::InvalidPhaseModelId);
    }

    if (catalog == nullptr) {
        return {};
    }
    if (catalog->compositions && !catalog->compositions->contains(definition.composition)) {
        return std::unexpected(MaterialRegistryError::UnresolvedComposition);
    }
    if (catalog->mechanical_models && !catalog->mechanical_models->contains(definition.mechanical)) {
        return std::unexpected(MaterialRegistryError::UnresolvedMechanicalModel);
    }
    if (catalog->thermal_models && !catalog->thermal_models->contains(definition.thermal)) {
        return std::unexpected(MaterialRegistryError::UnresolvedThermalModel);
    }
    if (catalog->electrical_models && !catalog->electrical_models->contains(definition.electrical)) {
        return std::unexpected(MaterialRegistryError::UnresolvedElectricalModel);
    }
    if (catalog->magnetic_models && !catalog->magnetic_models->contains(definition.magnetic)) {
        return std::unexpected(MaterialRegistryError::UnresolvedMagneticModel);
    }
    if (catalog->optical_models && !catalog->optical_models->contains(definition.optical)) {
        return std::unexpected(MaterialRegistryError::UnresolvedOpticalModel);
    }
    if (catalog->phase_models && !catalog->phase_models->contains(definition.phase)) {
        return std::unexpected(MaterialRegistryError::UnresolvedPhaseModel);
    }
    return {};
}

class MaterialRegistry {
public:
    MaterialRegistry() = default;

    explicit MaterialRegistry(MaterialModelCatalog catalog)
    {
        if (const auto validity = validate_material_model_catalog(catalog); !validity) {
            throw std::invalid_argument("material model catalog contains reserved ID 0");
        }
        model_catalog_ = std::move(catalog);
    }

    // Persistence-only transactional import. Catalog foreign keys and every
    // definition are checked before the canonical registry changes.
    [[nodiscard]] std::expected<void, MaterialRegistryError> restore(
        MaterialRegistryRevision revision,
        std::vector<MaterialDefinition> definitions,
        std::optional<MaterialModelCatalog> catalog = std::nullopt)
    {
        if (revision == std::numeric_limits<MaterialRegistryRevision>::max()) {
            return std::unexpected(MaterialRegistryError::RevisionExhausted);
        }
        // The current canonical registry exposes add-only mutation, so every
        // successful revision corresponds to exactly one live definition.
        if (!std::cmp_equal(revision, definitions.size())) {
            return std::unexpected(MaterialRegistryError::InvalidRevision);
        }
        if (catalog) {
            if (const auto validity = validate_material_model_catalog(*catalog); !validity) {
                return std::unexpected(validity.error());
            }
        }

        std::map<MaterialId, MaterialDefinition> restored;
        for (auto& definition : definitions) {
            const auto validity = validate_material_definition(definition, catalog ? &*catalog : nullptr);
            if (!validity) {
                return std::unexpected(validity.error());
            }
            const auto id = definition.id;
            if (!restored.try_emplace(id, std::move(definition)).second) {
                return std::unexpected(MaterialRegistryError::DuplicateMaterialId);
            }
        }

        definitions_ = std::move(restored);
        model_catalog_ = std::move(catalog);
        revision_ = revision;
        return {};
    }

    [[nodiscard]] MaterialMutationResult try_add(MaterialDefinition definition)
    {
        const auto validity = validate_material_definition(
            definition,
            model_catalog_ ? &*model_catalog_ : nullptr);
        if (!validity) {
            return std::unexpected(validity.error());
        }
        if (definitions_.contains(definition.id)) {
            return std::unexpected(MaterialRegistryError::DuplicateMaterialId);
        }
        if (revision_ == std::numeric_limits<MaterialRegistryRevision>::max()) {
            return std::unexpected(MaterialRegistryError::RevisionExhausted);
        }

        const auto id = definition.id;
        const auto [position, inserted] = definitions_.try_emplace(id, std::move(definition));
        static_cast<void>(position);
        if (!inserted) {
            return std::unexpected(MaterialRegistryError::DuplicateMaterialId);
        }
        ++revision_;
        return revision_;
    }

    // Compatibility wrapper for existing scenario construction. Callers that
    // need to distinguish invalid metadata, duplicates, and revision exhaustion
    // should use try_add().
    [[nodiscard]] bool add(MaterialDefinition definition)
    {
        return try_add(std::move(definition)).has_value();
    }

    [[nodiscard]] const MaterialDefinition* find(MaterialId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] bool contains(MaterialId id) const noexcept { return definitions_.contains(id); }

    [[nodiscard]] const MaterialDefinition& at(MaterialId id) const { return definitions_.at(id); }

    [[nodiscard]] const std::map<MaterialId, MaterialDefinition>& ordered_definitions() const noexcept
    {
        return definitions_;
    }

    [[nodiscard]] auto begin() const noexcept { return definitions_.cbegin(); }
    [[nodiscard]] auto end() const noexcept { return definitions_.cend(); }
    [[nodiscard]] bool empty() const noexcept { return definitions_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }
    [[nodiscard]] MaterialRegistryRevision revision() const noexcept { return revision_; }
    [[nodiscard]] const MaterialModelCatalog* model_catalog() const noexcept
    {
        return model_catalog_ ? &*model_catalog_ : nullptr;
    }

private:
    std::map<MaterialId, MaterialDefinition> definitions_;
    std::optional<MaterialModelCatalog> model_catalog_;
    MaterialRegistryRevision revision_{};
};

}  // namespace principia::materials
