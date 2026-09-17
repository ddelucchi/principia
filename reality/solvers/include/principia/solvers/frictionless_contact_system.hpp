#pragma once

#include <principia/solvers/frictionless_contact.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace principia::solvers {

// This ordering is part of the deterministic event-selection contract.
enum class FrictionlessContactSystemEventKind : std::uint8_t {
    StaticBoundary = 0,
    ParticlePair = 1,
};

struct FrictionlessContactSystemEvent2 {
    units::Duration time_from_start{units::seconds(0.0)};
    FrictionlessContactSystemEventKind kind{FrictionlessContactSystemEventKind::StaticBoundary};
    world::ParticleId first_particle;
    std::optional<world::ParticleId> second_particle;
    std::optional<boundaries::BoundaryId> boundary;
    spacetime::WorldPosition<2> contact_point;
    math::Vector<2, double> normal_from_second_to_first;
    math::Vector<2, units::Momentum> impulse_on_first;
    units::Energy energy_to_unresolved_internal_modes{units::joules(0.0)};

    friend bool operator==(const FrictionlessContactSystemEvent2&, const FrictionlessContactSystemEvent2&) =
        default;
};

struct FrictionlessContactParticleImpulse2 {
    world::ParticleId particle;
    math::Vector<2, units::Momentum> boundary_impulse;
    math::Vector<2, units::Momentum> internal_impulse;

    friend bool operator==(const FrictionlessContactParticleImpulse2&, const FrictionlessContactParticleImpulse2&) =
        default;
};

struct FrictionlessContactSystemDriftResult2 {
    // Both vectors are in strictly increasing persistent-particle-ID order.
    std::vector<ContactBody2> bodies;
    std::vector<FrictionlessContactParticleImpulse2> particle_impulses;
    math::Vector<2, units::Momentum> boundary_impulse;
    units::AngularMomentum boundary_angular_impulse{
        units::kilogram_square_metres_per_second(0.0)};
    // Integrated outward energy flux over this drift interval.
    units::Energy unresolved_internal_energy_flux{units::joules(0.0)};
    // Chronological, then kind, then persistent-ID order.
    std::vector<FrictionlessContactSystemEvent2> contacts;

    friend bool operator==(const FrictionlessContactSystemDriftResult2&,
                           const FrictionlessContactSystemDriftResult2&) = default;
};

struct FrictionlessContactSystemConfiguration {
    std::uint32_t maximum_events{64};
    std::size_t maximum_bodies{1024};
    std::size_t maximum_boundaries{1024};
    // Used for feature classification and verified contact-manifold residuals;
    // it never expands the simulated time horizon.
    double geometric_tolerance_metres{1.0e-12};
    // Groups already-admitted event times for coupled-contact detection. It
    // never admits or clamps a time of impact beyond dt.
    double simultaneous_time_tolerance_seconds{1.0e-12};

    friend bool operator==(const FrictionlessContactSystemConfiguration&,
                           const FrictionlessContactSystemConfiguration&) = default;
};

enum class FrictionlessContactSystemError : std::uint8_t {
    NonPositiveTimeStep,
    NonFiniteInput,
    NonPositiveMass,
    NonPositiveRadius,
    InvalidConstraint,
    InvalidConfiguration,
    BodyLimitExceeded,
    BoundaryLimitExceeded,
    InvalidIdentifier,
    DuplicateParticle,
    InvalidBoundaryRegistry,
    InvalidMaterialRegistry,
    InvalidMechanicalResponseRegistry,
    MissingMaterial,
    MissingMechanicalResponse,
    UnsupportedMechanicalResponse,
    InitialOverlap,
    UnsupportedCoupledSimultaneousContact,
    EventLimitExceeded,
    NumericalOverflow,
};

// Advances a copy of the supplied free discs through exactly dt. Collision
// detection is analytic and swept; the call is all-or-error and never mutates
// a registry or caller-owned body. Coupled simultaneous contacts are outside
// this foundation slice and are reported explicitly.
[[nodiscard]] std::expected<FrictionlessContactSystemDriftResult2, FrictionlessContactSystemError>
resolve_frictionless_contact_system_drift(
    std::vector<ContactBody2> bodies,
    const boundaries::BoundaryRegistry& boundaries,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    FrictionlessContactSystemConfiguration configuration = {});

}  // namespace principia::solvers
