#include <principia/ontology/registry.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace principia::ontology {
namespace {

template <typename Id>
void normalize_id_set(std::vector<Id>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename Id>
[[nodiscard]] bool contains_only_valid_ids(const std::vector<Id>& values) noexcept
{
    return std::ranges::all_of(values, [](Id id) { return static_cast<bool>(id); });
}

[[nodiscard]] constexpr bool valid(TheoryRelationKind value) noexcept
{
    switch (value) {
    case TheoryRelationKind::StructuralDependency:
    case TheoryRelationKind::LimitingTheory:
    case TheoryRelationKind::EffectiveTheory:
    case TheoryRelationKind::CoarseGraining:
    case TheoryRelationKind::Coupling:
    case TheoryRelationKind::SymmetryBreaking:
    case TheoryRelationKind::SemiclassicalApproximation:
    case TheoryRelationKind::UnknownCompletion:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(RelationDirection value) noexcept
{
    switch (value) {
    case RelationDirection::ThisToOther:
    case RelationDirection::OtherToThis:
    case RelationDirection::Bidirectional:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(ImplementationStatus value) noexcept
{
    switch (value) {
    case ImplementationStatus::MetadataOnly:
    case ImplementationStatus::Implemented:
    case ImplementationStatus::Experimental:
    case ImplementationStatus::Deprecated:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(EpistemicStatus value) noexcept
{
    switch (value) {
    case EpistemicStatus::Established:
    case EpistemicStatus::EffectiveEstablished:
    case EpistemicStatus::Approximation:
    case EpistemicStatus::Speculative:
    case EpistemicStatus::FictionalExtension:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(ModelBridgeKind value) noexcept
{
    switch (value) {
    case ModelBridgeKind::Projection:
    case ModelBridgeKind::Lifting:
    case ModelBridgeKind::SharedCanonicalState:
    case ModelBridgeKind::BidirectionalApproximation:
        return true;
    }
    return false;
}

[[nodiscard]] auto relation_key(const TheoryRelation& relation)
{
    return std::tuple{
        relation.other.value(),
        static_cast<std::uint8_t>(relation.kind),
        static_cast<std::uint8_t>(relation.direction),
        relation.limit ? relation.limit->value() : 0U,
        relation.verification.value()};
}

[[nodiscard]] auto edge_key(const TheoryEdge& edge)
{
    return std::tuple{
        edge.source.value(),
        edge.target.value(),
        static_cast<std::uint8_t>(edge.kind),
        edge.limit ? edge.limit->value() : 0U,
        edge.verification.value(),
        edge.bidirectional};
}

}  // namespace

TheoryRegistry::TheoryRegistry(
    std::vector<TheoryDescriptor> theories,
    std::vector<ModelBridgeDescriptor> bridges)
    : theories_(std::move(theories)), bridges_(std::move(bridges))
{
    std::sort(theories_.begin(), theories_.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    std::sort(bridges_.begin(), bridges_.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });

    std::set<std::string> names;
    TheoryId previous_id{};
    for (auto& descriptor : theories_) {
        if (!descriptor.id) {
            throw std::invalid_argument("theory IDs must be nonzero");
        }
        if (descriptor.id == previous_id) {
            throw std::invalid_argument("duplicate theory ID: " + std::to_string(descriptor.id.value()));
        }
        if (!state::has_non_whitespace(descriptor.name) || !names.insert(descriptor.name).second) {
            throw std::invalid_argument("theory names must be nonempty and unique: " + descriptor.name);
        }
        if (!descriptor.validity_domain) {
            throw std::invalid_argument("theory has no validity domain: " + descriptor.name);
        }
        if (!valid(descriptor.status) || !valid(descriptor.epistemic_status)) {
            throw std::invalid_argument("theory has an invalid status discriminant: " + descriptor.name);
        }
        if (!contains_only_valid_ids(descriptor.conserved) ||
            !contains_only_valid_ids(descriptor.constraints) ||
            !contains_only_valid_ids(descriptor.bridges) ||
            !contains_only_valid_ids(descriptor.boundaries)) {
            throw std::invalid_argument("theory contains a reserved metadata ID: " + descriptor.name);
        }
        const auto valid_channels = [](const state::ChannelSet& channels) {
            return std::ranges::all_of(channels.values(), [](state::StateChannelId id) {
                return static_cast<bool>(id);
            });
        };
        if (!valid_channels(descriptor.channels.reads) || !valid_channels(descriptor.channels.writes)) {
            throw std::invalid_argument("theory contains reserved state channel ID 0: " + descriptor.name);
        }
        if (!std::ranges::all_of(descriptor.relations, [](const TheoryRelation& relation) {
                return relation.other && valid(relation.kind) && valid(relation.direction);
            })) {
            throw std::invalid_argument("theory contains an invalid relation: " + descriptor.name);
        }

        normalize_id_set(descriptor.conserved);
        normalize_id_set(descriptor.constraints);
        normalize_id_set(descriptor.bridges);
        normalize_id_set(descriptor.boundaries);
        std::sort(descriptor.relations.begin(), descriptor.relations.end(), [](const auto& lhs, const auto& rhs) {
            return relation_key(lhs) < relation_key(rhs);
        });
        if (std::adjacent_find(descriptor.relations.begin(), descriptor.relations.end()) !=
            descriptor.relations.end()) {
            throw std::invalid_argument("duplicate relation on theory: " + descriptor.name);
        }
        previous_id = descriptor.id;
    }

    ModelBridgeId previous_bridge_id{};
    std::set<std::string> bridge_names;
    for (auto& bridge : bridges_) {
        if (!bridge.id) {
            throw std::invalid_argument("model bridge IDs must be nonzero");
        }
        if (bridge.id == previous_bridge_id) {
            throw std::invalid_argument("duplicate model bridge ID: " + std::to_string(bridge.id.value()));
        }
        if (!state::has_non_whitespace(bridge.name) || !bridge_names.insert(bridge.name).second) {
            throw std::invalid_argument("model bridge names must be nonempty and unique: " + bridge.name);
        }
        if (bridge.source == bridge.target || find(bridge.source) == nullptr || find(bridge.target) == nullptr) {
            throw std::invalid_argument("model bridge has invalid endpoints: " + bridge.name);
        }
        if (!valid(bridge.kind) || !contains_only_valid_ids(bridge.preserves)) {
            throw std::invalid_argument("model bridge has invalid metadata: " + bridge.name);
        }
        normalize_id_set(bridge.preserves);
        previous_bridge_id = bridge.id;
    }

    for (const auto& descriptor : theories_) {
        for (const auto& relation : descriptor.relations) {
            if (relation.other == descriptor.id || find(relation.other) == nullptr) {
                throw std::invalid_argument("theory has an invalid relation endpoint: " + descriptor.name);
            }
            if (relation.limit && !*relation.limit) {
                throw std::invalid_argument("theory relation contains an invalid limit ID: " + descriptor.name);
            }
        }
        for (const auto bridge_id : descriptor.bridges) {
            const auto* bridge = find(bridge_id);
            if (bridge == nullptr || (bridge->source != descriptor.id && bridge->target != descriptor.id)) {
                throw std::invalid_argument("theory references an unrelated or unknown model bridge: " + descriptor.name);
            }
        }
    }
}

const TheoryDescriptor* TheoryRegistry::find(TheoryId id) const noexcept
{
    const auto iterator = std::lower_bound(
        theories_.begin(), theories_.end(), id, [](const TheoryDescriptor& descriptor, TheoryId candidate) {
            return descriptor.id < candidate;
        });
    return iterator != theories_.end() && iterator->id == id ? &*iterator : nullptr;
}

const TheoryDescriptor* TheoryRegistry::find(std::string_view name) const noexcept
{
    const auto iterator = std::find_if(theories_.begin(), theories_.end(), [name](const auto& descriptor) {
        return descriptor.name == name;
    });
    return iterator != theories_.end() ? &*iterator : nullptr;
}

const ModelBridgeDescriptor* TheoryRegistry::find(ModelBridgeId id) const noexcept
{
    const auto iterator = std::lower_bound(
        bridges_.begin(), bridges_.end(), id, [](const ModelBridgeDescriptor& descriptor, ModelBridgeId candidate) {
            return descriptor.id < candidate;
        });
    return iterator != bridges_.end() && iterator->id == id ? &*iterator : nullptr;
}

std::vector<TheoryEdge> TheoryRegistry::edges() const
{
    std::vector<TheoryEdge> result;
    for (const auto& descriptor : theories_) {
        for (const auto& relation : descriptor.relations) {
            TheoryEdge edge{
                .source = descriptor.id,
                .target = relation.other,
                .kind = relation.kind,
                .limit = relation.limit,
                .verification = relation.verification,
                .bidirectional = relation.direction == RelationDirection::Bidirectional,
            };
            if (relation.direction == RelationDirection::OtherToThis) {
                std::swap(edge.source, edge.target);
            } else if (edge.bidirectional && edge.target < edge.source) {
                std::swap(edge.source, edge.target);
            }
            result.push_back(edge);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return edge_key(lhs) < edge_key(rhs);
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<TheoryEdge> TheoryRegistry::incoming(TheoryId id) const
{
    auto result = edges();
    std::erase_if(result, [id](const TheoryEdge& edge) {
        return edge.target != id && !(edge.bidirectional && edge.source == id);
    });
    return result;
}

std::vector<TheoryEdge> TheoryRegistry::outgoing(TheoryId id) const
{
    auto result = edges();
    std::erase_if(result, [id](const TheoryEdge& edge) {
        return edge.source != id && !(edge.bidirectional && edge.target == id);
    });
    return result;
}

std::vector<const TheoryDescriptor*> TheoryRegistry::implemented_theories() const
{
    std::vector<const TheoryDescriptor*> result;
    for (const auto& descriptor : theories_) {
        if (descriptor.status == ImplementationStatus::Implemented) {
            result.push_back(&descriptor);
        }
    }
    return result;
}

const TheoryRegistry& standard_theory_registry()
{
    static const TheoryRegistry registry = make_standard_theory_registry();
    return registry;
}

std::string_view to_string(TheoryRelationKind value) noexcept
{
    switch (value) {
    case TheoryRelationKind::StructuralDependency: return "structural dependency";
    case TheoryRelationKind::LimitingTheory: return "limiting theory";
    case TheoryRelationKind::EffectiveTheory: return "effective theory";
    case TheoryRelationKind::CoarseGraining: return "coarse graining";
    case TheoryRelationKind::Coupling: return "coupling";
    case TheoryRelationKind::SymmetryBreaking: return "symmetry breaking";
    case TheoryRelationKind::SemiclassicalApproximation: return "semiclassical approximation";
    case TheoryRelationKind::UnknownCompletion: return "unknown completion";
    }
    return "unknown relation";
}

std::string_view to_string(RelationDirection value) noexcept
{
    switch (value) {
    case RelationDirection::ThisToOther: return "this to other";
    case RelationDirection::OtherToThis: return "other to this";
    case RelationDirection::Bidirectional: return "bidirectional";
    }
    return "unknown direction";
}

std::string_view to_string(ImplementationStatus value) noexcept
{
    switch (value) {
    case ImplementationStatus::MetadataOnly: return "metadata only";
    case ImplementationStatus::Implemented: return "implemented";
    case ImplementationStatus::Experimental: return "experimental";
    case ImplementationStatus::Deprecated: return "deprecated";
    }
    return "unknown implementation status";
}

std::string_view to_string(EpistemicStatus value) noexcept
{
    switch (value) {
    case EpistemicStatus::Established: return "established";
    case EpistemicStatus::EffectiveEstablished: return "effective established";
    case EpistemicStatus::Approximation: return "approximation";
    case EpistemicStatus::Speculative: return "speculative";
    case EpistemicStatus::FictionalExtension: return "fictional extension";
    }
    return "unknown epistemic status";
}

std::string_view to_string(ModelBridgeKind value) noexcept
{
    switch (value) {
    case ModelBridgeKind::Projection: return "projection";
    case ModelBridgeKind::Lifting: return "lifting";
    case ModelBridgeKind::SharedCanonicalState: return "shared canonical state";
    case ModelBridgeKind::BidirectionalApproximation: return "bidirectional approximation";
    }
    return "unknown bridge kind";
}

std::string_view name_of(ConservedQuantityId id) noexcept
{
    switch (id.value()) {
    case 1: return "mass";
    case 2: return "linear momentum";
    case 3: return "angular momentum";
    case 4: return "energy";
    case 5: return "electric charge";
    case 6: return "probability";
    case 7: return "baryon number";
    case 8: return "magnetic flux";
    case 9: return "particle number";
    default: return "unknown conserved quantity";
    }
}

std::string_view name_of(ConstraintId id) noexcept
{
    switch (id.value()) {
    case 1: return "mass shell";
    case 2: return "quantum state normalization";
    case 3: return "Gauss electric";
    case 4: return "Gauss magnetic";
    case 5: return "gauge condition";
    case 6: return "Einstein Hamiltonian";
    case 7: return "Einstein momentum";
    case 8: return "equation of state";
    case 9: return "positive density";
    case 10: return "local equilibrium";
    case 11: return "strain compatibility";
    case 12: return "yield surface";
    case 13: return "crack irreversibility";
    case 14: return "solenoidal magnetic field";
    case 15: return "charge neutrality";
    case 16: return "fixed kinematic";
    default: return "unknown constraint";
    }
}

std::string_view name_of(ValidityDomainId id) noexcept
{
    switch (id.value()) {
    case 1: return "structural";
    case 2: return "stationary action";
    case 3: return "decohered classical";
    case 4: return "quantum state";
    case 5: return "flat spacetime";
    case 6: return "relativistic particle";
    case 7: return "low velocity (beta << 1)";
    case 8: return "classical field";
    case 9: return "relativistic electromagnetism";
    case 10: return "electrostatic";
    case 11: return "magnetostatic";
    case 12: return "geometric optics";
    case 13: return "classical curved spacetime";
    case 14: return "weak field and slow motion";
    case 15: return "nonrelativistic quantum";
    case 16: return "bound atomic";
    case 17: return "Born-Oppenheimer";
    case 18: return "quantum many-body";
    case 19: return "relativistic quantum field";
    case 20: return "low-energy nuclear";
    case 21: return "thermodynamic limit";
    case 22: return "local equilibrium";
    case 23: return "dilute gas";
    case 24: return "continuum (Kn << 1)";
    case 25: return "small strain";
    case 26: return "constitutive plasticity";
    case 27: return "fracture scale";
    case 28: return "hydrodynamic";
    case 29: return "inviscid";
    case 30: return "Newtonian fluid";
    case 31: return "near-equilibrium transport";
    case 32: return "collective plasma";
    case 33: return "magnetohydrodynamic";
    case 34: return "relativistic fluid";
    case 35: return "relativistic magnetofluid";
    case 36: return "stellar structure";
    case 37: return "compact object";
    case 38: return "cosmological scale";
    case 39: return "curvature below QFT cutoff";
    case 40: return "semiclassical backreaction";
    case 41: return "unknown quantum gravity";
    default: return "unknown validity domain";
    }
}

std::string_view name_of(BoundaryRequirementId id) noexcept
{
    switch (id.value()) {
    case 1: return "particle domain";
    case 2: return "classical field domain";
    case 3: return "electromagnetic interface";
    case 4: return "gravitational domain";
    case 5: return "quantum state domain";
    case 6: return "continuum flux";
    case 7: return "mechanical interface";
    case 8: return "thermal interface";
    case 9: return "plasma interface";
    case 10: return "cosmological domain";
    default: return "unknown boundary requirement";
    }
}

std::string_view name_of(LimitParameterId id) noexcept
{
    switch (id.value()) {
    case 1: return "classical decoherence";
    case 2: return "beta = v/c";
    case 3: return "quasi-static parameter";
    case 4: return "wavelength / structure scale";
    case 5: return "GM/(r c^2)";
    case 6: return "hbar / action";
    case 7: return "inverse particle number";
    case 8: return "Knudsen number";
    case 9: return "viscosity";
    case 10: return "plasma collisionality";
    default: return "unknown limit parameter";
    }
}

std::string_view name_of(VerificationSuiteId id) noexcept
{
    switch (id.value()) {
    case 0: return "none";
    case 1: return "test_relativistic_to_newtonian_limit";
    case 2: return "test_general_relativity_to_newtonian_gravity_limit";
    case 3: return "test_maxwell_quasi_static_limits";
    case 4: return "test_statistical_to_thermodynamic_limit";
    case 5: return "test_kinetic_to_hydrodynamic_limit";
    case 6: return "test_plasma_to_magnetohydrodynamic_limit";
    default: return "unknown verification suite";
    }
}

}  // namespace principia::ontology
