#include <principia/game/reality_test_001.hpp>
#include <principia/diagnostics/inspection.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    using namespace principia;

    auto scenario = game::make_reality_test_001();
    const solvers::NewtonianParticleSolver2 solver;
    const solvers::NewtonianContactEnvironment2 contact{
        scenario.materials,
        scenario.mechanical_responses,
        scenario.disc_colliders,
    };
    constexpr int steps = 120;
    const auto dt = units::seconds(1.0 / 120.0);

    for (int step = 0; step < steps; ++step) {
        const auto result = solver.advance(scenario.world, scenario.gravity, contact, dt);
        if (!result) {
            std::cerr << "Simulation step failed with error " << static_cast<int>(result.error()) << '\n';
            return 1;
        }
    }

    std::cout << "Reality Test 001\n"
              << "field: " << scenario.gravity.metadata().name << '\n'
              << "composition: transform-chain, canonical (priority, operator-id) order\n"
              << "time: " << units::in_seconds(scenario.world.simulation_time.elapsed()) << " s\n\n";

    bool passed = true;
    const diagnostics::InspectionContactContext2 inspection_contact{
        scenario.materials,
        scenario.mechanical_responses,
        scenario.disc_colliders,
    };
    const auto inspection = diagnostics::capture_world_inspection(
        scenario.world,
        scenario.gravity,
        inspection_contact);
    if (!inspection) {
        std::cerr << "World inspection failed with error " << static_cast<int>(inspection.error()) << '\n';
        return 1;
    }
    std::cout << std::fixed << std::setprecision(6);
    for (const auto& inspected : inspection->particles) {
        const auto role = scenario.roles.at(inspected.state.id);
        const auto vx = units::in_metres_per_second(inspected.velocity[0]);
        const auto vy = units::in_metres_per_second(inspected.velocity[1]);
        const auto x = units::in_metres(inspected.state.position[0]);
        const auto y = units::in_metres(inspected.state.position[1]);
        std::cout << std::setw(8) << game::role_name(role) << "  x=(" << x << ", " << y << ") m"
                  << "  v=(" << vx << ", " << vy << ") m/s";
        if (inspected.mechanical_contact) {
            std::cout << "  collider=" << units::in_metres(inspected.mechanical_contact->radius)
                      << " m  restitution="
                      << inspected.mechanical_contact->normal_coefficient_of_restitution;
        }
        std::cout << '\n';
        passed = passed && vx > 9.7 && std::abs(vy) < 1.0e-10;
    }

    std::cout << "\nstatic wall boundary: " << scenario.world.boundaries.at(scenario.static_wall_id).name << '\n';
    std::cout << (passed ? "PASS: player, rock, and sand fell sideways through one shared field.\n"
                         : "FAIL: shared-field response diverged.\n");
    return passed ? 0 : 2;
}
