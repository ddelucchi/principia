#pragma once

#include <principia/boundaries/boundary.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/math/vector.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/world.hpp>
#include <principia/world/collider.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace principia::diagnostics {

enum class InspectionError : std::uint8_t {
    InvalidFieldMetadata,
    NonFiniteParticleState,
    NonFiniteFieldSample,
    InvalidFrame,
    InvalidWorldClock,
    InvalidContactMetadata,
    MissingMaterial,
    MissingMechanicalResponse,
};

struct InspectionContactContext2 {
    const materials::MaterialRegistry& materials;
    const materials::MechanicalResponseRegistry& mechanical_responses;
    const world::DiscColliderRegistry2& disc_colliders;
};

struct InspectedMechanicalContact2 {
    units::Length radius{units::metres(0.0)};
    std::string material_name;
    std::string mechanical_response_name;
    double normal_coefficient_of_restitution{};
};

struct InspectedParticle2 {
    world::ParticleState2 state;
    math::Vector<2, units::Velocity> velocity;
    fields::GravityVector<2> effective_gravity;
    std::optional<InspectedMechanicalContact2> mechanical_contact;
};

struct WorldInspectionFrame2 {
    std::uint64_t tick{};
    spacetime::SimulationTime simulation_time{};
    std::uint64_t particle_revision{};
    std::uint64_t gravity_revision{};
    std::uint64_t boundary_revision{};
    std::uint64_t material_revision{};
    std::uint64_t mechanical_response_revision{};
    std::uint64_t collider_revision{};
    bool contact_metadata_included{};
    fields::FieldMetadata gravity_metadata;
    std::vector<InspectedParticle2> particles;
    std::vector<boundaries::BoundaryDefinition> boundaries;
};

namespace detail {

[[nodiscard]] inline std::expected<WorldInspectionFrame2, InspectionError> capture_world_inspection_impl(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const InspectionContactContext2* contact,
    spacetime::FrameId frame)
{
    if (!fields::valid_effective_gravity_metadata<2>(gravity.metadata())) {
        return std::unexpected(InspectionError::InvalidFieldMetadata);
    }
    if (!frame) {
        return std::unexpected(InspectionError::InvalidFrame);
    }
    const auto simulation_seconds = units::in_seconds(world.simulation_time.elapsed());
    if (!std::isfinite(simulation_seconds) || simulation_seconds < 0.0) {
        return std::unexpected(InspectionError::InvalidWorldClock);
    }
    WorldInspectionFrame2 result;
    result.tick = world.tick;
    result.simulation_time = world.simulation_time;
    result.particle_revision = world.particles.revision();
    result.gravity_revision = gravity.revision();
    result.boundary_revision = world.boundaries.revision();
    if (contact != nullptr) {
        result.material_revision = contact->materials.revision();
        result.mechanical_response_revision = contact->mechanical_responses.revision();
        result.collider_revision = contact->disc_colliders.revision();
        result.contact_metadata_included = true;
        for (const auto& [particle_id, collider] : contact->disc_colliders.ordered_colliders()) {
            if (particle_id != collider.particle || !world::validate_disc_collider(collider) ||
                world.particles.find(particle_id) == nullptr) {
                return std::unexpected(InspectionError::InvalidContactMetadata);
            }
        }
    }
    result.gravity_metadata = gravity.metadata();
    result.particles.reserve(world.particles.ordered_particles().size());
    for (const auto& [id, particle] : world.particles.ordered_particles()) {
        const auto velocity = world::velocity_of(particle);
        const auto mass = units::in_kilograms(particle.rest_mass);
        const auto momentum_x = units::in_kilogram_metres_per_second(particle.momentum[0]);
        const auto momentum_y = units::in_kilogram_metres_per_second(particle.momentum[1]);
        const auto valid_constraint = particle.constraint == world::KinematicConstraint::Free ||
                                      particle.constraint == world::KinematicConstraint::Fixed;
        const auto fixed_has_zero_momentum =
            particle.constraint != world::KinematicConstraint::Fixed ||
            (momentum_x == 0.0 && momentum_y == 0.0);
        const auto finite_particle =
            id && particle.id == id && particle.material && std::isfinite(mass) && mass > 0.0 &&
            std::isfinite(momentum_x) && std::isfinite(momentum_y) && valid_constraint &&
            fixed_has_zero_momentum && std::isfinite(units::in_metres(particle.position[0])) &&
            std::isfinite(units::in_metres(particle.position[1])) &&
            std::isfinite(units::in_metres_per_second(velocity[0])) &&
            std::isfinite(units::in_metres_per_second(velocity[1]));
        if (!finite_particle) {
            return std::unexpected(InspectionError::NonFiniteParticleState);
        }
        const auto sampled = gravity.sample(spacetime::Event<2>{world.simulation_time, particle.position, frame});
        if (!std::isfinite(units::in_metres_per_second_squared(sampled[0])) ||
            !std::isfinite(units::in_metres_per_second_squared(sampled[1]))) {
            return std::unexpected(InspectionError::NonFiniteFieldSample);
        }
        std::optional<InspectedMechanicalContact2> mechanical_contact;
        if (contact != nullptr) {
            const auto* material = contact->materials.find(particle.material);
            if (material == nullptr) {
                return std::unexpected(InspectionError::MissingMaterial);
            }
            const auto* collider = contact->disc_colliders.find(particle.id);
            if (collider != nullptr) {
                const auto* response = contact->mechanical_responses.find(material->mechanical);
                if (response == nullptr) {
                    return std::unexpected(InspectionError::MissingMechanicalResponse);
                }
                mechanical_contact = InspectedMechanicalContact2{
                    collider->radius,
                    material->name,
                    response->name,
                    response->normal_coefficient_of_restitution,
                };
            }
        }
        result.particles.push_back(InspectedParticle2{
            particle,
            velocity,
            sampled,
            std::move(mechanical_contact),
        });
    }
    result.boundaries.reserve(world.boundaries.size());
    for (const auto& [id, boundary] : world.boundaries) {
        static_cast<void>(id);
        result.boundaries.push_back(boundary);
    }
    return result;
}

}  // namespace detail

[[nodiscard]] inline std::expected<WorldInspectionFrame2, InspectionError> capture_world_inspection(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    spacetime::FrameId frame = spacetime::FrameId{1})
{
    return detail::capture_world_inspection_impl(world, gravity, nullptr, frame);
}

[[nodiscard]] inline std::expected<WorldInspectionFrame2, InspectionError> capture_world_inspection(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const InspectionContactContext2& contact,
    spacetime::FrameId frame = spacetime::FrameId{1})
{
    return detail::capture_world_inspection_impl(world, gravity, &contact, frame);
}

}  // namespace principia::diagnostics
