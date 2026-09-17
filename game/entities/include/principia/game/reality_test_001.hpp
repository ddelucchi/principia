#pragma once

#include <principia/boundaries/boundary.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/operators/gravity_operator.hpp>
#include <principia/world/collider.hpp>
#include <principia/world/world.hpp>

#include <map>
#include <string_view>
#include <utility>

namespace principia::game {

enum class EntityRole {
    Player,
    Rock,
    Sand,
};

[[nodiscard]] std::string_view role_name(EntityRole role) noexcept;

struct RealityTest001Scenario {
    explicit RealityTest001Scenario(operators::OperatedGravityField2 gravity_field)
        : gravity(std::move(gravity_field))
    {
    }

    world::WorldState2 world;
    materials::MaterialRegistry materials;
    materials::MechanicalResponseRegistry mechanical_responses;
    world::DiscColliderRegistry2 disc_colliders;
    operators::OperatedGravityField2 gravity;
    std::map<world::ParticleId, EntityRole> roles;
    boundaries::BoundaryId static_wall_id{};
};

[[nodiscard]] RealityTest001Scenario make_reality_test_001();

}  // namespace principia::game
