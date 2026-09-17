#pragma once

#include <principia/boundaries/boundary.hpp>
#include <principia/spacetime/time.hpp>
#include <principia/world/particle.hpp>

#include <cstdint>

namespace principia::world {

struct WorldState2 {
    ParticleStore2 particles;
    boundaries::BoundaryRegistry boundaries;
    std::uint64_t tick{};
    spacetime::SimulationTime simulation_time{};
};

}  // namespace principia::world
