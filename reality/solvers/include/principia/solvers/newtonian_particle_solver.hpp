#pragma once

#include <principia/conservation/balance.hpp>
#include <principia/diagnostics/report.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/math/vector.hpp>
#include <principia/scheduler/scheduler.hpp>
#include <principia/solvers/frictionless_contact_system.hpp>
#include <principia/spacetime/time.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/particle.hpp>
#include <principia/world/world.hpp>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace principia::solvers {

inline constexpr std::string_view newtonian_integrator_id = "NewtonianKickContactDriftKick";
inline constexpr std::uint32_t newtonian_step_contract_revision = 3;
inline constexpr std::uint32_t newtonian_integrator_revision = 3;
inline constexpr std::string_view deterministic_numeric_abi = "principia.ieee754.binary64.canonical-si";
inline constexpr std::uint32_t deterministic_numeric_abi_revision = 1;

enum class NewtonianStepError : std::uint8_t {
    NonPositiveTimeStep,
    NonFiniteTimeStep,
    NonFiniteSimulationTime,
    NonPositiveMass,
    NonFiniteParticleState,
    NonFiniteFieldSample,
    NumericalOverflow,
    InvalidConstraint,
    OutsideValidityDomain,
    SourceRevisionExhausted,
    StaleSnapshot,
    MissingParticle,
    InvalidProposal,
    CommitRejected,
    InvalidContactEnvironment,
    ContactResolutionFailed,
    ConstraintAuditFailed,
    ConservationAuditFailed,
    InvalidValidityConfiguration,
};

struct NewtonianValidityConfiguration {
    units::Velocity invariant_speed{units::metres_per_second(299'792'458.0)};
    double maximum_beta{0.01};

    friend bool operator==(const NewtonianValidityConfiguration&,
                           const NewtonianValidityConfiguration&) = default;
};

// Contact data is deliberately supplied as one immutable, revisioned context.
// The point-particle overloads remain useful for models that explicitly have no
// collision geometry; worlds with disc colliders must use these overloads.
struct NewtonianContactEnvironment2 {
    const materials::MaterialRegistry& materials;
    const materials::MechanicalResponseRegistry& mechanical_responses;
    const world::DiscColliderRegistry2& disc_colliders;
    FrictionlessContactSystemConfiguration configuration{};
};

struct MechanicsSourceStamp2 {
    std::uint64_t particle_revision{};
    std::uint64_t gravity_revision{};
    std::uint64_t boundary_revision{};
    std::uint64_t material_revision{};
    std::uint64_t mechanical_response_revision{};
    std::uint64_t collider_revision{};
    std::uint64_t tick{};
    spacetime::SimulationTime simulation_time{};
    NewtonianValidityConfiguration validity_configuration{};
    bool contact_enabled{};
    FrictionlessContactSystemConfiguration contact_configuration{};

    friend bool operator==(const MechanicsSourceStamp2&, const MechanicsSourceStamp2&) = default;
};

struct ParticleUpdate2 {
    world::ParticleId id;
    spacetime::WorldPosition<2> position;
    math::Vector<2, units::Momentum> momentum;
    fields::GravityVector<2> sampled_gravity;
    fields::GravityVector<2> sampled_gravity_after_drift;
    math::Vector<2, units::Momentum> initial_gravity_impulse;
    math::Vector<2, units::Momentum> final_gravity_impulse;
    math::Vector<2, units::Momentum> gravity_impulse;
    math::Vector<2, units::Momentum> momentum_after_contact_drift;
    math::Vector<2, units::Momentum> internal_contact_impulse;
    math::Vector<2, units::Momentum> constraint_reaction_impulse;
    math::Vector<2, units::Momentum> boundary_impulse;
    units::AngularMomentum boundary_angular_impulse{
        units::kilogram_square_metres_per_second(0.0)};

    friend bool operator==(const ParticleUpdate2&, const ParticleUpdate2&) = default;
};

struct NewtonianStepProposal2 {
    MechanicsSourceStamp2 source;
    units::Duration dt{units::seconds(0.0)};
    std::vector<ParticleUpdate2> updates;
    std::vector<FrictionlessContactSystemEvent2> contacts;
    units::Energy unresolved_internal_energy_flux{units::joules(0.0)};
    conservation::ParticleBalanceReport2 conservation;
    diagnostics::SolverStepDiagnostic diagnostic;
};

class NewtonianValidityEvaluator2 {
public:
    explicit constexpr NewtonianValidityEvaluator2(NewtonianValidityConfiguration configuration = {})
        : configuration_(configuration)
    {
    }

    [[nodiscard]] diagnostics::ValidityReport evaluate(const world::ParticleSnapshot2& snapshot) const;
    [[nodiscard]] constexpr NewtonianValidityConfiguration configuration() const noexcept { return configuration_; }

private:
    NewtonianValidityConfiguration configuration_;
};

class NewtonianParticleSolver2 {
public:
    explicit constexpr NewtonianParticleSolver2(
        NewtonianValidityEvaluator2 validity = NewtonianValidityEvaluator2{})
        : validity_(validity)
    {
    }

    [[nodiscard]] scheduler::SolverDescriptor descriptor() const;
    [[nodiscard]] constexpr NewtonianValidityConfiguration validity_configuration() const noexcept
    {
        return validity_.configuration();
    }

    [[nodiscard]] std::expected<NewtonianStepProposal2, NewtonianStepError> propose(
        const world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        units::Duration dt) const;

    [[nodiscard]] std::expected<NewtonianStepProposal2, NewtonianStepError> propose(
        const world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianContactEnvironment2& contact,
        units::Duration dt) const;

    [[nodiscard]] std::expected<void, NewtonianStepError> commit(
        world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianStepProposal2& proposal) const;

    [[nodiscard]] std::expected<void, NewtonianStepError> commit(
        world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianContactEnvironment2& contact,
        const NewtonianStepProposal2& proposal) const;

    [[nodiscard]] std::expected<NewtonianStepProposal2, NewtonianStepError> advance(
        world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        units::Duration dt) const;

    [[nodiscard]] std::expected<NewtonianStepProposal2, NewtonianStepError> advance(
        world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianContactEnvironment2& contact,
        units::Duration dt) const;

private:
    [[nodiscard]] std::expected<NewtonianStepProposal2, NewtonianStepError> propose_impl(
        const world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianContactEnvironment2* contact,
        units::Duration dt) const;

    [[nodiscard]] std::expected<void, NewtonianStepError> commit_impl(
        world::WorldState2& world,
        const fields::EffectiveGravityField<2>& gravity,
        const NewtonianContactEnvironment2* contact,
        const NewtonianStepProposal2& proposal) const;

    NewtonianValidityEvaluator2 validity_;
};

}  // namespace principia::solvers
