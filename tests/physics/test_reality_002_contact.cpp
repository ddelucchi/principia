#include "../test_support.hpp"

#include <principia/boundaries/boundary.hpp>
#include <principia/fields/field.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/solvers/frictionless_contact.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/state/channel.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/collider.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] principia::materials::MaterialDefinition material(
    std::uint32_t id,
    std::uint32_t mechanical_model)
{
    using namespace principia;
    return materials::MaterialDefinition{
        materials::MaterialId{id},
        "contact material " + std::to_string(id),
        materials::CompositionId{id},
        materials::MechanicalModelId{mechanical_model},
        materials::ThermalModelId{id},
        materials::ElectricalModelId{id},
        materials::MagneticModelId{id},
        materials::OpticalModelId{id},
        materials::PhaseModelId{id},
    };
}

struct ContactFixture {
    principia::materials::MaterialRegistry materials;
    principia::materials::MechanicalResponseRegistry responses;
    principia::boundaries::BoundaryRegistry boundaries;
};

[[nodiscard]] ContactFixture make_fixture()
{
    using namespace principia;
    ContactFixture fixture;
    if (!fixture.responses.try_add(materials::MechanicalResponseDefinition{
            materials::MechanicalModelId{1},
            "elastic frictionless",
            materials::ImpactResponseKind::FrictionlessRestitution,
            1.0}) ||
        !fixture.responses.try_add(materials::MechanicalResponseDefinition{
            materials::MechanicalModelId{2},
            "inelastic frictionless",
            materials::ImpactResponseKind::FrictionlessRestitution,
            0.5}) ||
        !fixture.materials.try_add(material(1, 1)) || !fixture.materials.try_add(material(2, 2))) {
        throw std::runtime_error("contact fixture registries rejected valid definitions");
    }
    const auto wall = fixture.boundaries.try_add(boundaries::BoundaryDefinition{
        boundaries::BoundaryId{1},
        "rigid swept-contact wall",
        boundaries::AxisAlignedBox2{
            spacetime::WorldPosition<2>{units::metres(5.0), units::metres(-2.0)},
            spacetime::WorldPosition<2>{units::metres(6.0), units::metres(2.0)},
        },
        {{state::standard_channels::position_id, boundaries::BoundaryConditionKind::MechanicallyRigid}},
    });
    if (!wall) {
        throw std::runtime_error("contact fixture boundary registry rejected a valid wall");
    }
    return fixture;
}

[[nodiscard]] principia::solvers::ContactBody2 body(
    std::uint64_t id,
    double x,
    double momentum_x,
    double mass,
    std::uint32_t material_id)
{
    using namespace principia;
    return solvers::ContactBody2{
        world::ParticleState2{
            world::ParticleId{id},
            spacetime::WorldPosition<2>{units::metres(x), units::metres(0.0)},
            math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(momentum_x),
                units::kilogram_metres_per_second(0.0),
            },
            units::kilograms(mass),
            materials::MaterialId{material_id},
            world::KinematicConstraint::Free,
        },
        units::metres(0.5),
    };
}

void test_swept_static_boundary_contact()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto fixture = make_fixture();
    const auto elastic = solvers::resolve_disc_against_static_boundaries(
        body(1, 0.0, 10.0, 1.0, 1),
        fixture.boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(elastic.has_value() && elastic->contacts.size() == 1,
            "a fast disc must hit a wall through swept contact instead of tunneling");
    require_near(units::in_seconds(elastic->contacts.front().time_from_start), 0.45, 1.0e-12,
                 "wall time of impact must be analytic");
    require_near(units::in_metres(elastic->body.state.position[0]), -1.0, 1.0e-12,
                 "elastic contact must drift through the exact remaining time");
    require_near(units::in_kilogram_metres_per_second(elastic->body.state.momentum[0]), -10.0, 1.0e-12,
                 "elastic rigid-wall impulse must reflect normal momentum");
    require_near(units::in_kilogram_metres_per_second(elastic->boundary_impulse[0]), -20.0, 1.0e-12,
                 "wall momentum exchange must be explicit in the ledger");
    require_near(units::in_joules(elastic->energy_to_unresolved_internal_modes), 0.0, 1.0e-12,
                 "elastic contact must not invent unresolved internal energy");

    const auto inelastic = solvers::resolve_disc_against_static_boundaries(
        body(1, 0.0, 10.0, 1.0, 2),
        fixture.boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(inelastic.has_value(), "a valid inelastic mechanical model must resolve");
    require_near(units::in_metres(inelastic->body.state.position[0]), 1.75, 1.0e-12,
                 "restitution must alter the post-contact cause, not script a position");
    require_near(units::in_kilogram_metres_per_second(inelastic->body.state.momentum[0]), -5.0, 1.0e-12,
                 "coefficient of restitution must determine outgoing normal momentum");
    require_near(units::in_joules(inelastic->energy_to_unresolved_internal_modes), 37.5, 1.0e-12,
                 "lost kinetic energy must be transferred to explicitly unresolved internal modes");

    const auto event_limited = solvers::resolve_disc_against_static_boundaries(
        body(1, 0.0, 10.0, 1.0, 1),
        fixture.boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0),
        solvers::FrictionlessContactConfiguration{0, 1.0e-12, 1.0e-12});
    require(!event_limited && event_limited.error() == solvers::FrictionlessContactError::EventLimitExceeded,
            "contact work must obey an explicit deterministic event budget");

    boundaries::BoundaryRegistry corner_boundaries;
    require(corner_boundaries.try_add(boundaries::BoundaryDefinition{
                boundaries::BoundaryId{1},
                "rounded-corner compatibility wall",
                boundaries::AxisAlignedBox2{
                    spacetime::WorldPosition<2>{units::metres(5.0), units::metres(5.0)},
                    spacetime::WorldPosition<2>{units::metres(6.0), units::metres(6.0)},
                },
                {{state::standard_channels::position_id,
                  boundaries::BoundaryConditionKind::MechanicallyRigid}},
            }).has_value(),
            "rounded-corner compatibility fixture must install");
    auto diagonal = body(1, 0.0, 10.0, 1.0, 1);
    diagonal.state.momentum[1] = units::kilogram_metres_per_second(10.0);
    const auto corner = solvers::resolve_disc_against_static_boundaries(
        diagonal,
        corner_boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(corner.has_value() && corner->contacts.size() == 1,
            "the compatibility API must delegate to exact rounded-corner contact geometry");
    require_near(
        units::in_seconds(corner->contacts.front().time_from_start),
        (5.0 - 0.5 / std::sqrt(2.0)) / 10.0,
        1.0e-12,
        "legacy single-disc entry must not retain the obsolete square-expanded corner TOI");
    require_near(corner->contacts.front().normal[0], -1.0 / std::sqrt(2.0), 1.0e-12,
                 "rounded-corner compatibility normal x must be radial");
    require_near(corner->contacts.front().normal[1], -1.0 / std::sqrt(2.0), 1.0e-12,
                 "rounded-corner compatibility normal y must be radial");
}

void test_contact_edge_states()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto fixture = make_fixture();
    const auto separating = solvers::resolve_disc_against_static_boundaries(
        body(1, 4.5, -1.0, 1.0, 1),
        fixture.boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(separating.has_value() && separating->contacts.empty(),
            "a body touching while separating must receive no spurious impulse");
    require_near(units::in_metres(separating->body.state.position[0]), 3.5, 1.0e-12,
                 "separating contact must continue ordinary drift");

    const auto overlap = solvers::resolve_disc_against_static_boundaries(
        body(1, 5.5, 0.0, 1.0, 1),
        fixture.boundaries,
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(!overlap && overlap.error() == solvers::FrictionlessContactError::InitialOverlap,
            "invalid penetrating initial state must fail explicitly instead of choosing an arbitrary normal");
}

void test_two_body_contact_conservation()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto fixture = make_fixture();
    const auto collision = solvers::resolve_disc_pair_drift(
        body(2, 2.0, 0.0, 3.0, 1),
        body(1, 0.0, 2.0, 1.0, 1),
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(collision.has_value() && collision->contact.has_value(),
            "unequal-mass discs must collide independent of input order");
    require(collision->first.state.id == world::ParticleId{1} &&
                collision->second.state.id == world::ParticleId{2},
            "pair results must canonicalize by persistent particle ID");
    require_near(units::in_kilogram_metres_per_second(collision->first.state.momentum[0]), -1.0, 1.0e-12,
                 "analytic unequal-mass elastic velocity for body one must match");
    require_near(units::in_kilogram_metres_per_second(collision->second.state.momentum[0]), 3.0, 1.0e-12,
                 "analytic unequal-mass elastic velocity for body two must match");
    require_near(
        units::in_kilogram_metres_per_second(
            collision->first.state.momentum[0] + collision->second.state.momentum[0]),
        2.0,
        1.0e-12,
        "closed two-body contact must conserve linear momentum");
    require_near(units::in_joules(collision->contact->energy_to_unresolved_internal_modes), 0.0, 1.0e-12,
                 "elastic two-body contact must conserve kinetic energy");

    const auto dissipative = solvers::resolve_disc_pair_drift(
        body(1, 0.0, 2.0, 1.0, 2),
        body(2, 2.0, 0.0, 3.0, 2),
        fixture.materials,
        fixture.responses,
        units::seconds(1.0));
    require(dissipative.has_value() && dissipative->contact.has_value(),
            "dissipative pair fixture must collide");
    require_near(units::in_joules(dissipative->contact->energy_to_unresolved_internal_modes), 1.125, 1.0e-12,
                 "restitution loss must equal one-half reduced mass times (1-e^2) times normal speed squared");
}

void test_contact_registry_validation()
{
    using namespace principia;
    using tests::require;

    world::DiscColliderRegistry2 colliders;
    require(!colliders.try_add(world::DiscCollider2{world::ParticleId{1}, units::metres(0.0)}) &&
                colliders.revision() == 0,
            "non-positive collider radius must not mutate canonical collider metadata");
    require(colliders.try_add(world::DiscCollider2{world::ParticleId{1}, units::metres(0.5)}).has_value(),
            "positive finite disc collider must install");
    require(!colliders.try_add(world::DiscCollider2{world::ParticleId{1}, units::metres(0.75)}) &&
                colliders.revision() == 1,
            "one particle cannot silently acquire duplicate collision shapes");
    const auto invalid_remove = colliders.try_remove(world::ParticleId{});
    require(!invalid_remove && invalid_remove.error() == world::ColliderRegistryError::InvalidParticleId &&
                colliders.revision() == 1,
            "removing the reserved particle ID must fail without mutating collider history");

    materials::MechanicalResponseRegistry responses;
    const auto whitespace_name = responses.try_add(materials::MechanicalResponseDefinition{
        materials::MechanicalModelId{1},
        " \t\n",
        materials::ImpactResponseKind::FrictionlessRestitution,
        1.0,
    });
    require(!whitespace_name && whitespace_name.error() == materials::MechanicalResponseError::EmptyName &&
                responses.revision() == 0,
            "mechanical response names must contain a visible character");
}

void test_contact_is_part_of_authoritative_mechanics()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto fixture = make_fixture();
    const fields::ConstantField<fields::GravityVector<2>, 2> zero_gravity{
        fields::effective_gravity_metadata<2>(),
        fields::GravityVector<2>{
            units::metres_per_second_squared(0.0),
            units::metres_per_second_squared(0.0),
        },
    };
    const solvers::NewtonianParticleSolver2 solver;

    world::WorldState2 elastic_world;
    auto elastic_body = body(1, 0.0, 10.0, 1.0, 1);
    require(elastic_world.particles.insert(elastic_body.state).has_value() &&
                elastic_world.boundaries.restore(
                    fixture.boundaries.revision(),
                    {fixture.boundaries.at(boundaries::BoundaryId{1})}).has_value(),
            "integrated contact world must install canonical state");
    world::DiscColliderRegistry2 elastic_colliders;
    require(elastic_colliders.try_add(
                world::DiscCollider2{world::ParticleId{1}, elastic_body.radius}).has_value(),
            "integrated contact world must install collider geometry");
    const solvers::NewtonianContactEnvironment2 elastic_contact{
        fixture.materials,
        fixture.responses,
        elastic_colliders,
    };
    const auto elastic = solver.propose(
        elastic_world,
        zero_gravity,
        elastic_contact,
        units::seconds(1.0));
    require(elastic.has_value() && elastic->source.contact_enabled &&
                elastic->updates.size() == 1 && elastic->contacts.size() == 1,
            "the transactional Newtonian proposal must contain swept wall contact");
    require_near(units::in_metres(elastic->updates.front().position[0]), -1.0, 1.0e-12,
                 "authoritative KDK drift must use the contact-resolved position");
    require_near(
        units::in_kilogram_metres_per_second(elastic->updates.front().momentum[0]),
        -10.0,
        1.0e-12,
        "authoritative KDK drift must use the contact-resolved momentum");
    require(elastic->conservation.within(),
            "contact boundary impulse must close every represented conservation ledger");
    auto changed_configuration = elastic_contact.configuration;
    ++changed_configuration.maximum_events;
    const solvers::NewtonianContactEnvironment2 configuration_changed_contact{
        fixture.materials,
        fixture.responses,
        elastic_colliders,
        changed_configuration,
    };
    const auto stale_configuration_commit = solver.commit(
        elastic_world,
        zero_gravity,
        configuration_changed_contact,
        *elastic);
    require(!stale_configuration_commit &&
                stale_configuration_commit.error() == solvers::NewtonianStepError::StaleSnapshot &&
                elastic_world.tick == 0,
            "contact budgets and tolerances must be immutable stamped proposal inputs");
    require(solver.commit(elastic_world, zero_gravity, elastic_contact, *elastic).has_value(),
            "a canonical contact proposal must commit atomically");
    require_near(
        units::in_metres(elastic_world.particles.find(world::ParticleId{1})->position[0]),
        -1.0,
        1.0e-12,
        "committed world state must equal the contact proposal");

    world::WorldState2 inelastic_world;
    auto inelastic_body = body(1, 0.0, 10.0, 1.0, 2);
    require(inelastic_world.particles.insert(inelastic_body.state).has_value() &&
                inelastic_world.boundaries.restore(
                    fixture.boundaries.revision(),
                    {fixture.boundaries.at(boundaries::BoundaryId{1})}).has_value(),
            "inelastic integrated contact world must install canonical state");
    world::DiscColliderRegistry2 inelastic_colliders;
    require(inelastic_colliders.try_add(
                world::DiscCollider2{world::ParticleId{1}, inelastic_body.radius}).has_value(),
            "inelastic integrated contact world must install collider geometry");
    const solvers::NewtonianContactEnvironment2 inelastic_contact{
        fixture.materials,
        fixture.responses,
        inelastic_colliders,
    };
    const auto inelastic = solver.propose(
        inelastic_world,
        zero_gravity,
        inelastic_contact,
        units::seconds(1.0));
    require(inelastic.has_value() && inelastic->conservation.within(),
            "inelastic contact must close through an explicit energy flux");
    require_near(
        units::in_joules(inelastic->unresolved_internal_energy_flux),
        37.5,
        1.0e-12,
        "the update must retain restitution loss as an explicit outward flux");
    require_near(
        units::in_joules(inelastic->conservation.kinetic_energy.outward_flux),
        37.5,
        1.0e-12,
        "the aggregate kinetic ledger must expose unresolved internal energy");
    require_near(
        units::in_joules(inelastic->conservation.entries.front().boundary_work),
        0.0,
        0.0,
        "a static wall must exchange momentum without being mislabeled as external work");

    auto forged = *inelastic;
    forged.contacts.front().time_from_start += units::seconds(0.01);
    require(!solver.commit(inelastic_world, zero_gravity, inelastic_contact, forged).has_value() &&
                inelastic_world.tick == 0,
            "forged contact provenance must be rejected without partial mutation");
    require(!solver.commit(inelastic_world, zero_gravity, *inelastic).has_value() &&
                inelastic_world.tick == 0,
            "a contact proposal cannot be committed through the point-particle contract");

    auto fixed_collider_particle = *inelastic_world.particles.find(world::ParticleId{1});
    fixed_collider_particle.constraint = world::KinematicConstraint::Fixed;
    fixed_collider_particle.momentum = math::Vector<2, units::Momentum>{
        units::kilogram_metres_per_second(0.0),
        units::kilogram_metres_per_second(0.0),
    };
    require(inelastic_world.particles.replace(fixed_collider_particle).has_value(),
            "fixed-collider rejection fixture must install a valid kinematic constraint");
    const auto unsupported_fixed_contact = solver.propose(
        inelastic_world,
        zero_gravity,
        inelastic_contact,
        units::seconds(0.1));
    require(!unsupported_fixed_contact &&
                unsupported_fixed_contact.error() == solvers::NewtonianStepError::InvalidContactEnvironment,
            "a fixed disc must be rejected explicitly until infinite-mass disc contact is implemented");
}

void test_two_body_contact_is_authoritative_and_global()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto fixture = make_fixture();
    const fields::ConstantField<fields::GravityVector<2>, 2> zero_gravity{
        fields::effective_gravity_metadata<2>(),
        fields::GravityVector<2>{
            units::metres_per_second_squared(0.0),
            units::metres_per_second_squared(0.0),
        },
    };
    const solvers::NewtonianParticleSolver2 solver;

    world::WorldState2 pair_world;
    const auto first = body(1, 0.0, 2.0, 1.0, 1);
    const auto second = body(2, 2.0, 0.0, 3.0, 1);
    require(pair_world.particles.insert(second.state).has_value() &&
                pair_world.particles.insert(first.state).has_value(),
            "authoritative pair fixture must accept reverse insertion order");
    world::DiscColliderRegistry2 colliders;
    require(colliders.try_add(world::DiscCollider2{second.state.id, second.radius}).has_value() &&
                colliders.try_add(world::DiscCollider2{first.state.id, first.radius}).has_value(),
            "authoritative pair fixture must accept reverse collider insertion order");
    const solvers::NewtonianContactEnvironment2 contact{
        fixture.materials,
        fixture.responses,
        colliders,
    };

    const auto proposal = solver.propose(pair_world, zero_gravity, contact, units::seconds(1.0));
    require(proposal.has_value() && proposal->updates.size() == 2 && proposal->contacts.size() == 1,
            "the authoritative drift must resolve particle pairs as one canonical system");
    const auto& event = proposal->contacts.front();
    require(event.kind == solvers::FrictionlessContactSystemEventKind::ParticlePair &&
                event.first_particle == world::ParticleId{1} &&
                event.second_particle == world::ParticleId{2} && !event.boundary,
            "authoritative pair provenance must use canonical persistent-ID ordering");
    require_near(units::in_seconds(event.time_from_start), 0.5, 1.0e-12,
                 "a contact exactly at the step-doubling midpoint must be resolved once");

    const auto& first_update = proposal->updates[0];
    const auto& second_update = proposal->updates[1];
    require(first_update.id == world::ParticleId{1} && second_update.id == world::ParticleId{2},
            "authoritative updates must remain snapshot-ID aligned");
    require_near(units::in_kilogram_metres_per_second(first_update.momentum[0]), -1.0, 1.0e-12,
                 "authoritative pair impulse must produce the analytic first momentum");
    require_near(units::in_kilogram_metres_per_second(second_update.momentum[0]), 3.0, 1.0e-12,
                 "authoritative pair impulse must produce the analytic second momentum");
    require_near(
        units::in_kilogram_metres_per_second(
            first_update.internal_contact_impulse[0] + second_update.internal_contact_impulse[0]),
        0.0,
        0.0,
        "pair impulses must cancel exactly in the canonical per-particle ledger");
    require(proposal->conservation.entries[0].internal_contact_impulse ==
                first_update.internal_contact_impulse &&
                proposal->conservation.entries[1].internal_contact_impulse ==
                    second_update.internal_contact_impulse,
            "the conservation audit must retain internal impulse provenance without treating it as a source");
    require_near(units::in_joules(proposal->unresolved_internal_energy_flux), 0.0, 0.0,
                 "an elastic pair must have no unresolved energy flux");
    require(proposal->conservation.within() &&
                proposal->conservation.linear_momentum_status == conservation::AuditStatus::Closed &&
                proposal->conservation.kinetic_energy_status == conservation::AuditStatus::Closed,
            "a closed elastic pair must close momentum and kinetic energy without external sources");
    require(solver.commit(pair_world, zero_gravity, contact, *proposal).has_value(),
            "the canonical pair proposal must commit atomically");

    world::WorldState2 dissipative_world;
    const auto dissipative_first = body(1, 0.0, 2.0, 1.0, 2);
    const auto dissipative_second = body(2, 2.0, 0.0, 3.0, 2);
    require(dissipative_world.particles.insert(dissipative_first.state).has_value() &&
                dissipative_world.particles.insert(dissipative_second.state).has_value(),
            "dissipative authoritative pair fixture must install");
    const auto dissipative = solver.propose(
        dissipative_world,
        zero_gravity,
        contact,
        units::seconds(1.0));
    require(dissipative.has_value() && dissipative->contacts.size() == 1 &&
                dissipative->conservation.within(),
            "dissipative pair contact must remain a valid authoritative proposal");
    require_near(
        units::in_joules(dissipative->unresolved_internal_energy_flux),
        1.125,
        1.0e-12,
        "pair restitution loss must be represented exactly once at system scope");
    require_near(
        units::in_joules(dissipative->conservation.kinetic_energy.outward_flux),
        1.125,
        1.0e-12,
        "the aggregate kinetic ledger must consume the canonical pair loss");
    require(dissipative->conservation.linear_momentum_status == conservation::AuditStatus::Closed &&
                dissipative->conservation.kinetic_energy_status ==
                    conservation::AuditStatus::BalancedWithSources,
            "internal dissipation must not be mislabeled as an external momentum source");

    world::WorldState2 limited_world;
    require(limited_world.particles.insert(first.state).has_value() &&
                limited_world.particles.insert(second.state).has_value(),
            "resource-limit fixture must install both particles");
    auto limited_configuration = contact.configuration;
    limited_configuration.maximum_bodies = 1;
    const solvers::NewtonianContactEnvironment2 limited_contact{
        fixture.materials,
        fixture.responses,
        colliders,
        limited_configuration,
    };
    const auto limited = solver.propose(
        limited_world,
        zero_gravity,
        limited_contact,
        units::seconds(1.0));
    require(!limited && limited.error() == solvers::NewtonianStepError::InvalidContactEnvironment,
            "contact resource-budget violations must fail before numerical resolution");
}

}  // namespace

void test_reality_002_contact()
{
    test_swept_static_boundary_contact();
    test_contact_edge_states();
    test_two_body_contact_conservation();
    test_contact_registry_validation();
    test_contact_is_part_of_authoritative_mechanics();
    test_two_body_contact_is_authoritative_and_global();
}
