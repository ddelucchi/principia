#include <principia/game/reality_test_001.hpp>

#include <principia/fields/field.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/math/vector.hpp>
#include <principia/units/quantity.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace principia::game {

namespace {

inline constexpr materials::MaterialId stone_material{1};
inline constexpr materials::MaterialId sand_material{2};
inline constexpr materials::MaterialId metal_material{3};
inline constexpr materials::MaterialId organic_material{4};

[[nodiscard]] materials::MaterialDefinition material(
    materials::MaterialId id,
    std::string name,
    std::uint32_t model_base)
{
    return materials::MaterialDefinition{
        id,
        std::move(name),
        materials::CompositionId{model_base},
        materials::MechanicalModelId{model_base},
        materials::ThermalModelId{model_base},
        materials::ElectricalModelId{model_base},
        materials::MagneticModelId{model_base},
        materials::OpticalModelId{model_base},
        materials::PhaseModelId{model_base},
    };
}

[[nodiscard]] materials::MechanicalResponseDefinition mechanical_response(
    materials::MechanicalModelId id,
    std::string name,
    double restitution)
{
    return materials::MechanicalResponseDefinition{
        id,
        std::move(name),
        materials::ImpactResponseKind::FrictionlessRestitution,
        restitution,
    };
}

[[nodiscard]] world::ParticleState2 particle(
    world::ParticleId id,
    units::Length x,
    units::Length y,
    units::Mass mass,
    materials::MaterialId material_id)
{
    return world::ParticleState2{
        id,
        spacetime::WorldPosition<2>{x, y},
        math::Vector<2, units::Momentum>{
            units::kilogram_metres_per_second(0.0),
            units::kilogram_metres_per_second(0.0),
        },
        mass,
        material_id,
        world::KinematicConstraint::Free,
    };
}

[[nodiscard]] operators::OperatedGravityField2 make_operated_gravity()
{
    const fields::GravityVector<2> ordinary_gravity{
        units::metres_per_second_squared(0.0),
        units::metres_per_second_squared(-9.81),
    };
    auto base = std::make_unique<fields::ConstantField<fields::GravityVector<2>, 2>>(
        fields::effective_gravity_metadata<2>(), ordinary_gravity);

    operators::GravityOperatorGraph graph{operators::CompositionPolicy::TransformChain};
    const auto installed = graph.add(operators::GravityOperatorNode{
        operators::OperatorId{1},
        100,
        operators::CircularRegion2{
            spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)},
            units::metres(20.0),
        },
        operators::RotateGravityQuarterTurns{1},
    });
    if (!installed) {
        throw std::runtime_error("Reality Test 001 gravity operator could not be installed");
    }
    return operators::OperatedGravityField2{std::move(base), std::move(graph)};
}

}  // namespace

std::string_view role_name(EntityRole role) noexcept
{
    switch (role) {
    case EntityRole::Player:
        return "player";
    case EntityRole::Rock:
        return "rock";
    case EntityRole::Sand:
        return "sand";
    }
    return "unknown";
}

RealityTest001Scenario make_reality_test_001()
{
    RealityTest001Scenario scenario{make_operated_gravity()};

    const auto add_material = [&scenario](materials::MaterialDefinition definition) {
        if (!scenario.materials.try_add(std::move(definition))) {
            throw std::runtime_error("Reality Test 001 material registry rejected a definition");
        }
    };
    add_material(material(stone_material, "stone", 1));
    add_material(material(sand_material, "sand", 2));
    add_material(material(metal_material, "metal", 3));
    add_material(material(organic_material, "organic", 4));

    const auto add_response = [&scenario](materials::MechanicalResponseDefinition definition) {
        if (!scenario.mechanical_responses.try_add(std::move(definition))) {
            throw std::runtime_error("Reality Test 001 mechanical response registry rejected a definition");
        }
    };
    add_response(mechanical_response(materials::MechanicalModelId{1}, "stone impact", 0.35));
    add_response(mechanical_response(materials::MechanicalModelId{2}, "sand impact", 0.05));
    add_response(mechanical_response(materials::MechanicalModelId{3}, "metal impact", 0.70));
    add_response(mechanical_response(materials::MechanicalModelId{4}, "organic impact", 0.15));

    constexpr world::ParticleId player_id{1};
    constexpr world::ParticleId rock_id{2};
    constexpr world::ParticleId sand_id{3};

    const auto add_particle = [&scenario](world::ParticleState2 definition) {
        if (!scenario.world.particles.insert(std::move(definition))) {
            throw std::runtime_error("Reality Test 001 particle store rejected a definition");
        }
    };
    add_particle(particle(
        player_id, units::metres(-2.0), units::metres(0.0), units::kilograms(80.0), organic_material));
    add_particle(particle(
        rock_id, units::metres(0.0), units::metres(0.0), units::kilograms(5.0), stone_material));
    add_particle(particle(
        sand_id, units::metres(2.0), units::metres(0.0), units::kilograms(0.1), sand_material));
    const auto add_collider = [&scenario](world::DiscCollider2 collider) {
        if (!scenario.disc_colliders.try_add(std::move(collider))) {
            throw std::runtime_error("Reality Test 001 collider registry rejected a definition");
        }
    };
    add_collider(world::DiscCollider2{player_id, units::metres(0.30)});
    add_collider(world::DiscCollider2{rock_id, units::metres(0.22)});
    add_collider(world::DiscCollider2{sand_id, units::metres(0.16)});
    scenario.roles.emplace(player_id, EntityRole::Player);
    scenario.roles.emplace(rock_id, EntityRole::Rock);
    scenario.roles.emplace(sand_id, EntityRole::Sand);

    scenario.static_wall_id = boundaries::BoundaryId{1};
    const auto wall_added = scenario.world.boundaries.try_emplace(
        scenario.static_wall_id,
        boundaries::BoundaryDefinition{
            scenario.static_wall_id,
            "static wall",
            boundaries::AxisAlignedBox2{
                spacetime::WorldPosition<2>{units::metres(12.0), units::metres(-4.0)},
                spacetime::WorldPosition<2>{units::metres(13.0), units::metres(4.0)},
            },
            {
                {state::standard_channels::position_id, boundaries::BoundaryConditionKind::MechanicallyRigid},
                {state::standard_channels::temperature_id, boundaries::BoundaryConditionKind::ThermallyConductive},
                {state::standard_channels::electric_field_id,
                 boundaries::BoundaryConditionKind::ElectricallyInsulating},
            },
        });
    if (!wall_added) {
        throw std::runtime_error("Reality Test 001 boundary registry rejected the static wall");
    }

    return scenario;
}

}  // namespace principia::game
