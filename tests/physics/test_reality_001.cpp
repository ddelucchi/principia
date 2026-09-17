#include "../test_support.hpp"

#include <principia/diagnostics/inspection.hpp>
#include <principia/game/reality_test_001.hpp>
#include <principia/diagnostics/inspection.hpp>
#include <principia/serialization/snapshot.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <cstddef>

void test_reality_001()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    auto scenario = game::make_reality_test_001();
    const solvers::NewtonianParticleSolver2 solver;
    const auto dt = units::seconds(1.0 / 120.0);
    const solvers::NewtonianContactEnvironment2 contact{
        scenario.materials,
        scenario.mechanical_responses,
        scenario.disc_colliders,
    };
    const auto proposal = solver.propose(scenario.world, scenario.gravity, contact, dt);
    require(proposal.has_value(), "Reality Test 001 step should be admissible");
    require(proposal->updates.size() == 3, "player, rock, and sand should all be sampled");
    require(proposal->diagnostic.field_samples == 18,
            "kick-drift-kick plus step-doubling must account for every common field sample");

    for (const auto& update : proposal->updates) {
        require_near(units::in_metres_per_second_squared(update.sampled_gravity[0]), 9.81, 1.0e-12,
                     "circular operator must rotate downward gravity sideways");
        require_near(units::in_metres_per_second_squared(update.sampled_gravity[1]), 0.0, 1.0e-12,
                     "rotated gravity must have no vertical component");
    }

    const auto committed = solver.commit(scenario.world, scenario.gravity, contact, *proposal);
    require(committed.has_value(), "transactional proposal should commit once");
    const auto stale_commit = solver.commit(scenario.world, scenario.gravity, contact, *proposal);
    require(!stale_commit.has_value() && stale_commit.error() == solvers::NewtonianStepError::StaleSnapshot,
            "stale proposal must be rejected without a second mutation");

    const spacetime::Event<2> outside{
        spacetime::SimulationTime{},
        spacetime::WorldPosition<2>{units::metres(30.0), units::metres(0.0)},
        spacetime::FrameId{1},
    };
    const auto outside_gravity = scenario.gravity.sample(outside);
    require_near(units::in_metres_per_second_squared(outside_gravity[0]), 0.0, 1.0e-12,
                 "outside the operator the base field must remain unchanged");
    require_near(units::in_metres_per_second_squared(outside_gravity[1]), -9.81, 1.0e-12,
                 "outside gravity must remain downward");

    auto simulation_a = game::make_reality_test_001();
    auto simulation_b = game::make_reality_test_001();
    const solvers::NewtonianContactEnvironment2 contact_a{
        simulation_a.materials,
        simulation_a.mechanical_responses,
        simulation_a.disc_colliders,
    };
    const solvers::NewtonianContactEnvironment2 contact_b{
        simulation_b.materials,
        simulation_b.mechanical_responses,
        simulation_b.disc_colliders,
    };
    for (std::size_t step = 0; step < 60; ++step) {
        require(solver.advance(simulation_a.world, simulation_a.gravity, contact_a, dt).has_value(),
                "first deterministic run failed");
        for (int render_observation = 0; render_observation < 3; ++render_observation) {
            for (const auto& [id, particle] : simulation_a.world.particles.ordered_particles()) {
                static_cast<void>(id);
                const spacetime::Event<2> observation{
                    simulation_a.world.simulation_time, particle.position, spacetime::FrameId{1}};
                static_cast<void>(simulation_a.gravity.sample(observation));
            }
        }
        require(solver.advance(simulation_b.world, simulation_b.gravity, contact_b, dt).has_value(),
                "second deterministic run failed");
    }

    const auto& state_a = simulation_a.world.particles.ordered_particles();
    const auto& state_b = simulation_b.world.particles.ordered_particles();
    require(state_a.size() == state_b.size(), "deterministic runs must retain the same particles");
    auto left = state_a.begin();
    auto right = state_b.begin();
    for (; left != state_a.end(); ++left, ++right) {
        require(left->first == right->first, "canonical iteration order must match");
        require(left->second.position == right->second.position && left->second.momentum == right->second.momentum,
                "presentation observations must not alter committed simulation state");
    }

    require(scenario.world.boundaries.contains(scenario.static_wall_id), "the static wall must be an explicit boundary");

    const diagnostics::InspectionContactContext2 inspection_contact{
        scenario.materials,
        scenario.mechanical_responses,
        scenario.disc_colliders,
    };
    const auto inspection = diagnostics::capture_world_inspection(
        scenario.world,
        scenario.gravity,
        inspection_contact);
    require(inspection.has_value() && inspection->contact_metadata_included &&
                inspection->particles.size() == 3,
            "the shared inspector must expose the committed contact model without presentation-owned state");
    for (const auto& inspected : inspection->particles) {
        require(inspected.mechanical_contact.has_value(),
                "every Reality Test entity must expose its canonical collider and material response");
    }
    auto orphan_colliders = scenario.disc_colliders;
    require(orphan_colliders.try_add(
                world::DiscCollider2{world::ParticleId{999}, units::metres(0.1)}).has_value(),
            "inspection rejection fixture must install locally valid orphan geometry");
    const diagnostics::InspectionContactContext2 orphan_contact{
        scenario.materials,
        scenario.mechanical_responses,
        orphan_colliders,
    };
    const auto invalid_inspection = diagnostics::capture_world_inspection(
        scenario.world,
        scenario.gravity,
        orphan_contact);
    require(!invalid_inspection &&
                invalid_inspection.error() == diagnostics::InspectionError::InvalidContactMetadata,
            "inspection must reject contact metadata that does not resolve into canonical particles");

    const auto captured = serialization::capture_snapshot_v1(scenario.world, scenario.gravity.operator_graph());
    require(captured.has_value(), "explicit V1 snapshot capture should succeed");
    const auto encoded = serialization::encode_snapshot_v1(*captured);
    require(encoded.has_value(), "canonical V1 snapshot encoding should succeed");
    const auto decoded = serialization::decode_snapshot_v1(*encoded);
    require(decoded.has_value() && *decoded == *captured, "V1 snapshot must round-trip deterministically");
    require(!serialization::decode_snapshot_v1("principia.snapshot 99").has_value(),
            "unknown schema versions must be rejected explicitly");

    const auto invalid_frame = diagnostics::capture_world_inspection(
        scenario.world,
        scenario.gravity,
        spacetime::FrameId{});
    require(!invalid_frame && invalid_frame.error() == diagnostics::InspectionError::InvalidFrame,
            "inspection must reject a reserved reference-frame ID");
    scenario.world.simulation_time = spacetime::SimulationTime{units::seconds(-1.0)};
    const auto invalid_clock = diagnostics::capture_world_inspection(scenario.world, scenario.gravity);
    require(!invalid_clock && invalid_clock.error() == diagnostics::InspectionError::InvalidWorldClock,
            "inspection must not sample fields at an invalid canonical simulation time");
}
