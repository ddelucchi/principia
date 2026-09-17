#include "../test_support.hpp"

#include <principia/fields/field.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/game/reality_test_001.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/operators/gravity_operator.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/collider.hpp>
#include <principia/world/world.hpp>

#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] principia::math::Vector<2, principia::units::Momentum> momentum(double x, double y)
{
    using namespace principia;
    return math::Vector<2, units::Momentum>{
        units::kilogram_metres_per_second(x),
        units::kilogram_metres_per_second(y),
    };
}

[[nodiscard]] principia::world::ParticleState2 particle(
    std::uint64_t id,
    double x,
    double y,
    double mass,
    principia::world::KinematicConstraint constraint = principia::world::KinematicConstraint::Free,
    double momentum_x = 0.0,
    double momentum_y = 0.0)
{
    using namespace principia;
    return world::ParticleState2{
        world::ParticleId{id},
        spacetime::WorldPosition<2>{units::metres(x), units::metres(y)},
        momentum(momentum_x, momentum_y),
        units::kilograms(mass),
        materials::MaterialId{1},
        constraint,
    };
}

[[nodiscard]] principia::world::WorldState2 one_particle_world(
    principia::world::ParticleState2 state)
{
    principia::world::WorldState2 world;
    if (!world.particles.insert(std::move(state))) {
        throw std::runtime_error("invalid solver-contract test particle");
    }
    return world;
}

[[nodiscard]] auto gravity_field(double x, double y)
{
    using namespace principia;
    return fields::ConstantField<fields::GravityVector<2>, 2>{
        fields::effective_gravity_metadata<2>(),
        fields::GravityVector<2>{
            units::metres_per_second_squared(x),
            units::metres_per_second_squared(y),
        },
    };
}

[[nodiscard]] principia::boundaries::BoundaryDefinition rigid_boundary(std::uint64_t id)
{
    using namespace principia;
    return boundaries::BoundaryDefinition{
        boundaries::BoundaryId{id},
        "transaction stamp fixture",
        boundaries::AxisAlignedBox2{
            spacetime::WorldPosition<2>{units::metres(2.0), units::metres(-1.0)},
            spacetime::WorldPosition<2>{units::metres(3.0), units::metres(1.0)},
        },
        {{state::standard_channels::position_id, boundaries::BoundaryConditionKind::MechanicallyRigid}},
    };
}

[[nodiscard]] principia::materials::MechanicalResponseDefinition response(std::uint32_t id)
{
    using namespace principia;
    return materials::MechanicalResponseDefinition{
        materials::MechanicalModelId{id},
        "solver-contract frictionless response " + std::to_string(id),
        materials::ImpactResponseKind::FrictionlessRestitution,
        1.0,
    };
}

[[nodiscard]] principia::materials::MaterialDefinition material_definition(std::uint32_t id)
{
    using namespace principia;
    return materials::MaterialDefinition{
        materials::MaterialId{id},
        "solver-contract material " + std::to_string(id),
        materials::CompositionId{id},
        materials::MechanicalModelId{id},
        materials::ThermalModelId{id},
        materials::ElectricalModelId{id},
        materials::MagneticModelId{id},
        materials::OpticalModelId{id},
        materials::PhaseModelId{id},
    };
}

void test_fixed_constraint_and_balances()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    auto world = one_particle_world(particle(1, 2.0, 3.0, 2.0, world::KinematicConstraint::Fixed));
    const auto gravity = gravity_field(0.0, -10.0);
    const solvers::NewtonianParticleSolver2 solver;
    const auto proposal = solver.propose(world, gravity, units::seconds(0.1));
    require(proposal.has_value(), "a canonical fixed particle should produce a mechanics proposal");
    require(proposal->updates.front().position == world.particles.find(world::ParticleId{1})->position,
            "a fixed particle must preserve position exactly");
    require(proposal->updates.front().momentum == momentum(0.0, 0.0),
            "a fixed particle must preserve zero canonical momentum");
    require_near(
        units::in_kilogram_metres_per_second(proposal->updates.front().constraint_reaction_impulse[1]),
        2.0,
        1.0e-12,
        "the support reaction must exactly cancel gravity impulse");
    require(proposal->diagnostic.constraints.satisfied && proposal->diagnostic.constraints.records.size() == 1,
            "fixed constraints must publish an executable per-particle residual");
    require(proposal->conservation.entries.size() == 1 && proposal->conservation.within(),
            "mass, linear momentum, angular momentum, and kinetic energy must close with attributed sources");
    require(proposal->conservation.total_mechanical_energy_status ==
                conservation::AuditStatus::MissingRepresentation,
            "total mechanical energy must remain explicitly unavailable without a potential representation");
}

void test_source_stamp_rejects_every_read_resource()
{
    using namespace principia;
    using tests::require;

    const solvers::NewtonianParticleSolver2 solver;
    const auto dt = units::seconds(1.0 / 120.0);

    auto particle_changed = game::make_reality_test_001();
    const auto particle_proposal = solver.propose(particle_changed.world, particle_changed.gravity, dt);
    require(particle_proposal.has_value(), "particle stale-source fixture must propose");
    require(particle_changed.world.particles.apply_impulse(world::ParticleId{1}, momentum(1.0, 0.0)).has_value(),
            "controlled impulse fixture must mutate the particle revision");
    const auto particle_commit = solver.commit(particle_changed.world, particle_changed.gravity, *particle_proposal);
    require(!particle_commit && particle_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a proposal must become stale after particle mutation");

    auto gravity_changed = game::make_reality_test_001();
    const auto gravity_proposal = solver.propose(gravity_changed.world, gravity_changed.gravity, dt);
    require(gravity_proposal.has_value(), "gravity stale-source fixture must propose");
    require(gravity_changed.gravity.install_operator(operators::GravityOperatorNode{
                operators::OperatorId{99},
                200,
                operators::CircularRegion2{
                    spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)}, units::metres(1.0)},
                operators::ScaleGravity{0.5}}),
            "valid gravity mutation fixture must install");
    const auto gravity_commit = solver.commit(gravity_changed.world, gravity_changed.gravity, *gravity_proposal);
    require(!gravity_commit && gravity_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a proposal must become stale after gravity-operator mutation");

    auto boundary_changed = game::make_reality_test_001();
    materials::MechanicalResponseRegistry boundary_responses;
    world::DiscColliderRegistry2 boundary_colliders;
    require(boundary_responses.try_add(response(4)).has_value() &&
                boundary_colliders.try_add(
                    world::DiscCollider2{world::ParticleId{1}, units::metres(0.5)}).has_value(),
            "contact source-stamp fixture metadata must install");
    const solvers::NewtonianContactEnvironment2 boundary_contact{
        boundary_changed.materials,
        boundary_responses,
        boundary_colliders,
    };
    const auto boundary_proposal =
        solver.propose(boundary_changed.world, boundary_changed.gravity, boundary_contact, dt);
    require(boundary_proposal.has_value(), "boundary stale-source fixture must propose");
    require(boundary_changed.world.boundaries.try_add(rigid_boundary(99)).has_value(),
            "valid boundary mutation fixture must install");
    const auto boundary_commit =
        solver.commit(boundary_changed.world, boundary_changed.gravity, boundary_contact, *boundary_proposal);
    require(!boundary_commit && boundary_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a proposal must become stale after boundary mutation");

    auto material_changed = game::make_reality_test_001();
    materials::MechanicalResponseRegistry material_responses;
    world::DiscColliderRegistry2 material_colliders;
    require(material_responses.try_add(response(4)).has_value() &&
                material_colliders.try_add(
                    world::DiscCollider2{world::ParticleId{1}, units::metres(0.5)}).has_value(),
            "material source-stamp fixture metadata must install");
    const solvers::NewtonianContactEnvironment2 material_contact{
        material_changed.materials,
        material_responses,
        material_colliders,
    };
    const auto material_proposal =
        solver.propose(material_changed.world, material_changed.gravity, material_contact, dt);
    require(material_proposal.has_value(), "material stale-source fixture must propose");
    require(material_changed.materials.try_add(material_definition(99)).has_value(),
            "material stale-source fixture must mutate its registry");
    const auto material_commit =
        solver.commit(material_changed.world, material_changed.gravity, material_contact, *material_proposal);
    require(!material_commit && material_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a contact proposal must become stale after material mutation");

    auto response_changed = game::make_reality_test_001();
    materials::MechanicalResponseRegistry response_responses;
    world::DiscColliderRegistry2 response_colliders;
    require(response_responses.try_add(response(4)).has_value() &&
                response_colliders.try_add(
                    world::DiscCollider2{world::ParticleId{1}, units::metres(0.5)}).has_value(),
            "mechanical-response source-stamp fixture metadata must install");
    const solvers::NewtonianContactEnvironment2 response_contact{
        response_changed.materials,
        response_responses,
        response_colliders,
    };
    const auto response_proposal =
        solver.propose(response_changed.world, response_changed.gravity, response_contact, dt);
    require(response_proposal.has_value(), "mechanical-response stale-source fixture must propose");
    require(response_responses.try_add(response(99)).has_value(),
            "mechanical-response stale-source fixture must mutate its registry");
    const auto response_commit =
        solver.commit(response_changed.world, response_changed.gravity, response_contact, *response_proposal);
    require(!response_commit && response_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a contact proposal must become stale after mechanical-response mutation");

    auto collider_changed = game::make_reality_test_001();
    materials::MechanicalResponseRegistry collider_responses;
    world::DiscColliderRegistry2 collider_colliders;
    require(collider_responses.try_add(response(4)).has_value() &&
                collider_colliders.try_add(
                    world::DiscCollider2{world::ParticleId{1}, units::metres(0.5)}).has_value(),
            "collider source-stamp fixture metadata must install");
    const solvers::NewtonianContactEnvironment2 collider_contact{
        collider_changed.materials,
        collider_responses,
        collider_colliders,
    };
    const auto collider_proposal =
        solver.propose(collider_changed.world, collider_changed.gravity, collider_contact, dt);
    require(collider_proposal.has_value(), "collider stale-source fixture must propose");
    require(collider_colliders.try_add(
                world::DiscCollider2{world::ParticleId{2}, units::metres(0.25)}).has_value(),
            "collider stale-source fixture must mutate its registry");
    const auto collider_commit =
        solver.commit(collider_changed.world, collider_changed.gravity, collider_contact, *collider_proposal);
    require(!collider_commit && collider_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "a contact proposal must become stale after collider mutation");
}

void test_forged_proposals_are_atomic()
{
    using namespace principia;
    using tests::require;

    const solvers::NewtonianParticleSolver2 solver;
    const auto dt = units::seconds(1.0 / 120.0);
    auto scenario = game::make_reality_test_001();
    const auto canonical = solver.propose(scenario.world, scenario.gravity, dt);
    require(canonical.has_value(), "forged-proposal fixture must propose");
    const auto initial = scenario.world.particles.snapshot();

    auto forged = *canonical;
    forged.updates.front().position[0] += units::metres(1.0);
    const auto forged_result = solver.commit(scenario.world, scenario.gravity, forged);
    require(!forged_result && forged_result.error() == solvers::NewtonianStepError::InvalidProposal &&
                scenario.world.particles.snapshot() == initial,
            "a forged update must be rejected without partial mutation");

    auto incomplete = *canonical;
    incomplete.updates.pop_back();
    const auto incomplete_result = solver.commit(scenario.world, scenario.gravity, incomplete);
    require(!incomplete_result && incomplete_result.error() == solvers::NewtonianStepError::InvalidProposal &&
                scenario.world.particles.snapshot() == initial,
            "an incomplete proposal must be rejected atomically");

    auto duplicate = *canonical;
    duplicate.updates[1].id = duplicate.updates[0].id;
    const auto duplicate_result = solver.commit(scenario.world, scenario.gravity, duplicate);
    require(!duplicate_result && duplicate_result.error() == solvers::NewtonianStepError::InvalidProposal &&
                scenario.world.particles.snapshot() == initial,
            "a duplicate-body proposal must be rejected atomically");

    auto nonfinite = *canonical;
    nonfinite.updates.front().momentum[0] =
        units::kilogram_metres_per_second(std::numeric_limits<double>::quiet_NaN());
    const auto nonfinite_result = solver.commit(scenario.world, scenario.gravity, nonfinite);
    require(!nonfinite_result && nonfinite_result.error() == solvers::NewtonianStepError::InvalidProposal &&
                scenario.world.particles.snapshot() == initial,
            "a non-finite proposal must be rejected atomically");
}

void test_numerical_and_validity_boundaries()
{
    using namespace principia;
    using tests::require;

    const solvers::NewtonianParticleSolver2 solver;
    auto world = one_particle_world(particle(1, 0.0, 0.0, 1.0));
    const auto ordinary_gravity = gravity_field(0.0, -10.0);
    const auto nan_dt = solver.propose(
        world,
        ordinary_gravity,
        units::seconds(std::numeric_limits<double>::quiet_NaN()));
    require(!nan_dt && nan_dt.error() == solvers::NewtonianStepError::NonFiniteTimeStep,
            "NaN timestep must have an explicit error");

    const auto nan_gravity = gravity_field(std::numeric_limits<double>::quiet_NaN(), 0.0);
    const auto nan_field = solver.propose(world, nan_gravity, units::seconds(0.1));
    require(!nan_field && nan_field.error() == solvers::NewtonianStepError::NonFiniteFieldSample,
            "non-finite field samples must be rejected before state construction");

    const solvers::NewtonianParticleSolver2 narrow_solver{
        solvers::NewtonianValidityEvaluator2{
            solvers::NewtonianValidityConfiguration{units::metres_per_second(100.0), 0.01}}};
    const auto crossing = narrow_solver.propose(world, ordinary_gravity, units::seconds(0.2));
    require(!crossing && crossing.error() == solvers::NewtonianStepError::OutsideValidityDomain,
            "a step that crosses the Newtonian beta limit must be rejected even when its initial state is valid");

    world::ParticleSnapshot2 tie_snapshot;
    tie_snapshot.particles.push_back(particle(2, 0.0, 0.0, 1.0, world::KinematicConstraint::Free, 0.5, 0.0));
    tie_snapshot.particles.push_back(particle(1, 0.0, 0.0, 1.0, world::KinematicConstraint::Free, -0.5, 0.0));
    const solvers::NewtonianValidityEvaluator2 evaluator{
        solvers::NewtonianValidityConfiguration{units::metres_per_second(100.0), 0.01}};
    const auto tied_report = evaluator.evaluate(tie_snapshot);
    require(tied_report.valid && tied_report.worst_particle_id == 1,
            "validity worst-body ties must resolve by persistent particle ID, not storage order");
}

void test_validity_configuration_is_transactional()
{
    using namespace principia;
    using tests::require;

    const solvers::NewtonianValidityConfiguration producer_configuration{
        units::metres_per_second(299'792'458.0),
        0.02,
    };
    const solvers::NewtonianValidityConfiguration consumer_configuration{
        units::metres_per_second(299'792'458.0),
        0.03,
    };
    const solvers::NewtonianParticleSolver2 producer{
        solvers::NewtonianValidityEvaluator2{producer_configuration}};
    const solvers::NewtonianParticleSolver2 consumer{
        solvers::NewtonianValidityEvaluator2{consumer_configuration}};
    require(producer.validity_configuration() == producer_configuration &&
                consumer.validity_configuration() == consumer_configuration,
            "solver validity configuration must be publicly inspectable exactly");

    auto scenario = game::make_reality_test_001();
    const auto proposal = producer.propose(
        scenario.world,
        scenario.gravity,
        units::seconds(1.0 / 120.0));
    require(proposal.has_value(),
            "both validity configurations must accept the cross-solver transaction fixture");
    require(proposal->source.validity_configuration == producer_configuration,
            "proposal provenance must stamp the exact validity configuration");
    const auto initial_particles = scenario.world.particles.snapshot();
    const auto initial_tick = scenario.world.tick;
    const auto initial_time = scenario.world.simulation_time;
    const auto cross_solver_commit =
        consumer.commit(scenario.world, scenario.gravity, *proposal);
    require(!cross_solver_commit &&
                cross_solver_commit.error() == solvers::NewtonianStepError::StaleSnapshot &&
                scenario.world.particles.snapshot() == initial_particles &&
                scenario.world.tick == initial_tick &&
                scenario.world.simulation_time == initial_time,
            "a solver must reject another validity configuration's proposal before mutation even when both accept it");

    const auto require_invalid_proposal = [&scenario](
                                              solvers::NewtonianValidityConfiguration configuration,
                                              std::string_view message) {
        const solvers::NewtonianParticleSolver2 invalid_solver{
            solvers::NewtonianValidityEvaluator2{configuration}};
        const auto result = invalid_solver.propose(
            scenario.world,
            scenario.gravity,
            units::seconds(1.0 / 120.0));
        require(!result &&
                    result.error() == solvers::NewtonianStepError::InvalidValidityConfiguration,
                message);
    };
    require_invalid_proposal(
        solvers::NewtonianValidityConfiguration{
            units::metres_per_second(std::numeric_limits<double>::quiet_NaN()),
            0.01,
        },
        "non-finite invariant speed must have the explicit validity-configuration cause");
    require_invalid_proposal(
        solvers::NewtonianValidityConfiguration{units::metres_per_second(0.0), 0.01},
        "non-positive invariant speed must have the explicit validity-configuration cause");
    require_invalid_proposal(
        solvers::NewtonianValidityConfiguration{
            units::metres_per_second(299'792'458.0),
            std::numeric_limits<double>::quiet_NaN(),
        },
        "non-finite beta limit must have the explicit validity-configuration cause");
    require_invalid_proposal(
        solvers::NewtonianValidityConfiguration{
            units::metres_per_second(299'792'458.0),
            0.0,
        },
        "non-positive beta limit must have the explicit validity-configuration cause");

    const solvers::NewtonianParticleSolver2 invalid_commit_solver{
        solvers::NewtonianValidityEvaluator2{solvers::NewtonianValidityConfiguration{
            units::metres_per_second(299'792'458.0),
            std::numeric_limits<double>::quiet_NaN(),
        }}};
    const auto invalid_commit =
        invalid_commit_solver.commit(scenario.world, scenario.gravity, *proposal);
    require(!invalid_commit &&
                invalid_commit.error() ==
                    solvers::NewtonianStepError::InvalidValidityConfiguration &&
                scenario.world.particles.snapshot() == initial_particles &&
                scenario.world.tick == initial_tick &&
                scenario.world.simulation_time == initial_time,
            "commit must prevalidate its solver validity configuration before comparing NaN-bearing provenance");

    materials::MechanicalResponseRegistry empty_responses;
    world::DiscColliderRegistry2 empty_colliders;
    const solvers::NewtonianContactEnvironment2 valid_contact{
        scenario.materials,
        empty_responses,
        empty_colliders,
    };
    const auto contact_proposal = producer.propose(
        scenario.world,
        scenario.gravity,
        valid_contact,
        units::seconds(1.0 / 120.0));
    require(contact_proposal.has_value(),
            "empty valid contact context must produce the contact-provenance fixture");
    auto invalid_contact_configuration = valid_contact.configuration;
    invalid_contact_configuration.geometric_tolerance_metres =
        std::numeric_limits<double>::quiet_NaN();
    const solvers::NewtonianContactEnvironment2 invalid_contact{
        scenario.materials,
        empty_responses,
        empty_colliders,
        invalid_contact_configuration,
    };
    const auto invalid_contact_commit = producer.commit(
        scenario.world,
        scenario.gravity,
        invalid_contact,
        *contact_proposal);
    require(!invalid_contact_commit &&
                invalid_contact_commit.error() ==
                    solvers::NewtonianStepError::InvalidContactEnvironment &&
                scenario.world.particles.snapshot() == initial_particles &&
                scenario.world.tick == initial_tick &&
                scenario.world.simulation_time == initial_time,
            "commit must prevalidate NaN-bearing contact provenance before source-stamp comparison");

    invalid_contact_configuration = valid_contact.configuration;
    invalid_contact_configuration.maximum_bodies = 0;
    const solvers::NewtonianContactEnvironment2 zero_capacity_contact{
        scenario.materials,
        empty_responses,
        empty_colliders,
        invalid_contact_configuration,
    };
    const auto zero_capacity_commit = producer.commit(
        scenario.world,
        scenario.gravity,
        zero_capacity_contact,
        *contact_proposal);
    require(!zero_capacity_commit &&
                zero_capacity_commit.error() ==
                    solvers::NewtonianStepError::InvalidContactEnvironment &&
                scenario.world.particles.snapshot() == initial_particles &&
                scenario.world.tick == initial_tick &&
                scenario.world.simulation_time == initial_time,
            "commit must prevalidate structurally invalid contact capacities before provenance comparison");
}

void test_integrator_claim_is_honest()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    auto world = one_particle_world(particle(1, 0.0, 0.0, 1.0));
    const auto gravity = gravity_field(10.0, 0.0);
    const solvers::NewtonianParticleSolver2 solver;
    for (int step = 0; step < 10; ++step) {
        const auto result = solver.advance(world, gravity, units::seconds(0.1));
        require(result.has_value(), "ballistic convergence fixture must advance");
        require(result->diagnostic.error.integration_status == diagnostics::EstimateStatus::Estimated,
                "kick-drift-kick must publish its step-doubling integration estimate");
    }
    const auto* final_particle = world.particles.find(world::ParticleId{1});
    require(final_particle != nullptr, "ballistic fixture particle must remain present");
    require_near(units::in_metres(final_particle->position[0]), 5.0, 1.0e-12,
                 "kick-drift-kick must reproduce the analytic constant-acceleration trajectory");
}

void test_kick_drift_kick_converges_at_second_order()
{
    using namespace principia;
    using tests::require;

    const fields::AnalyticField<fields::GravityVector<2>, 2> harmonic_gravity{
        fields::effective_gravity_metadata<2>(),
        [](const spacetime::Event<2>& event) {
            return fields::GravityVector<2>{
                units::metres_per_second_squared(-units::in_metres(event.position[0])),
                units::metres_per_second_squared(0.0),
            };
        },
    };
    const solvers::NewtonianParticleSolver2 solver;
    const auto integrate = [&solver, &harmonic_gravity](double dt_seconds, int steps) {
        auto world = one_particle_world(particle(1, 1.0, 0.0, 1.0));
        double largest_reported_estimate = 0.0;
        for (int step = 0; step < steps; ++step) {
            const auto result = solver.advance(world, harmonic_gravity, units::seconds(dt_seconds));
            if (!result) {
                throw std::runtime_error("harmonic convergence fixture failed to advance");
            }
            largest_reported_estimate =
                std::max(largest_reported_estimate, result->diagnostic.error.integration_error);
        }
        const auto* final = world.particles.find(world::ParticleId{1});
        if (final == nullptr) {
            throw std::runtime_error("harmonic convergence particle disappeared");
        }
        const auto position_error = units::in_metres(final->position[0]) - std::cos(1.0);
        const auto momentum_error =
            units::in_kilogram_metres_per_second(final->momentum[0]) + std::sin(1.0);
        return std::pair{std::hypot(position_error, momentum_error), largest_reported_estimate};
    };

    const auto coarse = integrate(0.1, 10);
    const auto fine = integrate(0.05, 20);
    require(coarse.second > 0.0 && fine.second > 0.0,
            "a spatially varying field must produce a nonzero step-doubling estimate");
    require(fine.first < coarse.first / 3.5,
            "halving the step must approach the analytic oscillator at second-order rate");
}

}  // namespace

void test_solver_contracts()
{
    test_fixed_constraint_and_balances();
    test_source_stamp_rejects_every_read_resource();
    test_forged_proposals_are_atomic();
    test_numerical_and_validity_boundaries();
    test_validity_configuration_is_transactional();
    test_integrator_claim_is_honest();
    test_kick_drift_kick_converges_at_second_order();
}
