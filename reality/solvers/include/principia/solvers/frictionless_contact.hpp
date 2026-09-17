#pragma once

#include <principia/boundaries/boundary.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/math/vector.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/collider.hpp>
#include <principia/world/particle.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace principia::solvers {

struct ContactBody2 {
    world::ParticleState2 state;
    units::Length radius;

    friend bool operator==(const ContactBody2&, const ContactBody2&) = default;
};

struct FrictionlessContactEvent2 {
    units::Duration time_from_start{units::seconds(0.0)};
    spacetime::WorldPosition<2> center_at_contact;
    math::Vector<2, double> normal;
    math::Vector<2, units::Momentum> impulse_on_first;
    std::optional<world::ParticleId> second_particle;
    std::optional<boundaries::BoundaryId> boundary;
    units::Energy energy_to_unresolved_internal_modes{units::joules(0.0)};

    friend bool operator==(const FrictionlessContactEvent2&, const FrictionlessContactEvent2&) = default;
};

struct ContactDriftResult2 {
    ContactBody2 body;
    math::Vector<2, units::Momentum> boundary_impulse;
    units::Energy energy_to_unresolved_internal_modes{units::joules(0.0)};
    std::vector<FrictionlessContactEvent2> contacts;

    friend bool operator==(const ContactDriftResult2&, const ContactDriftResult2&) = default;
};

struct PairContactDriftResult2 {
    ContactBody2 first;
    ContactBody2 second;
    std::optional<FrictionlessContactEvent2> contact;

    friend bool operator==(const PairContactDriftResult2&, const PairContactDriftResult2&) = default;
};

struct FrictionlessContactConfiguration {
    std::uint32_t maximum_boundary_events{8};
    double geometric_tolerance_metres{1.0e-12};
    double time_fraction_tolerance{1.0e-12};
};

enum class FrictionlessContactError : std::uint8_t {
    NonPositiveTimeStep,
    NonFiniteInput,
    NonPositiveMass,
    NonPositiveRadius,
    InvalidConstraint,
    MissingMaterial,
    MissingMechanicalResponse,
    UnsupportedMechanicalResponse,
    InvalidConfiguration,
    InitialOverlap,
    EventLimitExceeded,
    NumericalOverflow,
    DuplicateParticle,
    UnsupportedCoupledSimultaneousContact,
};

// Compatibility projections for one boundary body and one particle pair.
// Both delegate to the canonical whole-system resolver; they do not implement
// an independent collision geometry or event-ordering path.
[[nodiscard]] std::expected<ContactDriftResult2, FrictionlessContactError>
resolve_disc_against_static_boundaries(
    ContactBody2 body,
    const boundaries::BoundaryRegistry& boundaries,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    FrictionlessContactConfiguration configuration = {});

[[nodiscard]] std::expected<PairContactDriftResult2, FrictionlessContactError>
resolve_disc_pair_drift(
    ContactBody2 left,
    ContactBody2 right,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    double geometric_tolerance_metres = 1.0e-12);

}  // namespace principia::solvers
