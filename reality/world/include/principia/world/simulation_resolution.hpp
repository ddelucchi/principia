#pragma once

#include <principia/ontology/theory.hpp>
#include <principia/world/chunk_grid.hpp>

#include <map>

namespace principia::world {

class ChunkResolutionProfile {
public:
    [[nodiscard]] bool set(ontology::TheoryId theory, ResolutionLevel level)
    {
        if (!theory || !valid_resolution_level(level)) {
            return false;
        }
        levels_[theory] = level;
        return true;
    }

    [[nodiscard]] ResolutionLevel for_theory(ontology::TheoryId theory) const noexcept
    {
        const auto found = levels_.find(theory);
        return found == levels_.end() ? ResolutionLevel::Dormant : found->second;
    }

    [[nodiscard]] const std::map<ontology::TheoryId, ResolutionLevel>& ordered_levels() const noexcept
    {
        return levels_;
    }

private:
    std::map<ontology::TheoryId, ResolutionLevel> levels_;
};

}  // namespace principia::world
