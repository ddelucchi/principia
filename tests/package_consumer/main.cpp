#include <principia/game/reality_test_001.hpp>

int main()
{
    const auto scenario = principia::game::make_reality_test_001();
    return scenario.world.particles.ordered_particles().size() == 3U ? 0 : 1;
}
