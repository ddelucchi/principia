#include "../test_support.hpp"

#include <principia/boundaries/boundary.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/solvers/frictionless_contact_system.hpp>
#include <principia/state/channel.hpp>
#include <principia/units/quantity.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ContactContext {
    principia::boundaries::BoundaryRegistry boundaries;
    principia::materials::MaterialRegistry materials;
    principia::materials::MechanicalResponseRegistry responses;
};

[[nodiscard]] principia::materials::MaterialDefinition material(
    std::uint32_t id,
    std::uint32_t mechanical_model)
{
    using namespace principia;
    return materials::MaterialDefinition{
        materials::MaterialId{id},
        "system contact material " + std::to_string(id),
        materials::CompositionId{id},
        materials::MechanicalModelId{mechanical_model},
        materials::ThermalModelId{id},
        materials::ElectricalModelId{id},
        materials::MagneticModelId{id},
        materials::OpticalModelId{id},
        materials::PhaseModelId{id},
    };
}

[[nodiscard]] ContactContext make_context()
{
    using namespace principia;
    ContactContext context;
    if (!context.responses.try_add(materials::MechanicalResponseDefinition{
            materials::MechanicalModelId{1},
            "elastic",
            materials::ImpactResponseKind::FrictionlessRestitution,
            1.0}) ||
        !context.responses.try_add(materials::MechanicalResponseDefinition{
            materials::MechanicalModelId{2},
            "half restitution",
            materials::ImpactResponseKind::FrictionlessRestitution,
            0.5}) ||
        !context.materials.try_add(material(1, 1)) ||
        !context.materials.try_add(material(2, 2))) {
        throw std::runtime_error("failed to create valid system-contact registries");
    }
    return context;
}

[[nodiscard]] principia::solvers::ContactBody2 body(
    std::uint64_t id,
    double x,
    double y,
    double momentum_x,
    double momentum_y,
    double mass = 1.0,
    std::uint32_t material_id = 1,
    double radius = 0.5)
{
    using namespace principia;
    return solvers::ContactBody2{
        world::ParticleState2{
            world::ParticleId{id},
            spacetime::WorldPosition<2>{units::metres(x), units::metres(y)},
            math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(momentum_x),
                units::kilogram_metres_per_second(momentum_y),
            },
            units::kilograms(mass),
            materials::MaterialId{material_id},
            world::KinematicConstraint::Free,
        },
        units::metres(radius),
    };
}

void add_wall(
    ContactContext& context,
    std::uint64_t id,
    double minimum_x,
    double minimum_y,
    double maximum_x,
    double maximum_y)
{
    using namespace principia;
    const auto added = context.boundaries.try_add(boundaries::BoundaryDefinition{
        boundaries::BoundaryId{id},
        "system wall " + std::to_string(id),
        boundaries::AxisAlignedBox2{
            spacetime::WorldPosition<2>{units::metres(minimum_x), units::metres(minimum_y)},
            spacetime::WorldPosition<2>{units::metres(maximum_x), units::metres(maximum_y)},
        },
        {{state::standard_channels::position_id,
          boundaries::BoundaryConditionKind::MechanicallyRigid}},
    });
    if (!added) {
        throw std::runtime_error("failed to add a valid system-contact wall");
    }
}

void test_sequential_pair_events_and_internal_ledgers()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    const std::vector<solvers::ContactBody2> shuffled{
        body(3, 8.0, 0.0, 0.0, 0.0),
        body(1, 0.0, 0.0, 4.0, 0.0),
        body(2, 3.0, 0.0, 0.0, 0.0),
    };
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        shuffled,
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(2.0));
    require(result.has_value(), "a separated three-disc chain must resolve");
    require(result->bodies.size() == 3 && result->contacts.size() == 2,
            "the event loop must process multiple sequential pair contacts");
    require(result->bodies[0].state.id == world::ParticleId{1} &&
                result->bodies[1].state.id == world::ParticleId{2} &&
                result->bodies[2].state.id == world::ParticleId{3},
            "system output must canonicalize shuffled bodies by persistent ID");
    require_near(units::in_seconds(result->contacts[0].time_from_start), 0.5, 1.0e-12,
                 "first swept pair time must be analytic");
    require_near(units::in_seconds(result->contacts[1].time_from_start), 1.5, 1.0e-12,
                 "second swept pair time must include prior event time");
    require(result->contacts[0].first_particle == world::ParticleId{1} &&
                result->contacts[0].second_particle == world::ParticleId{2} &&
                result->contacts[1].first_particle == world::ParticleId{2} &&
                result->contacts[1].second_particle == world::ParticleId{3},
            "pair records must retain canonical persistent participants");
    require_near(units::in_metres(result->bodies[0].state.position[0]), 2.0, 1.0e-12,
                 "first disc must stop at its first exact contact");
    require_near(units::in_metres(result->bodies[1].state.position[0]), 7.0, 1.0e-12,
                 "middle disc must carry momentum between exact events");
    require_near(units::in_metres(result->bodies[2].state.position[0]), 10.0, 1.0e-12,
                 "last disc must drift for the exact post-contact remainder");
    require_near(
        units::in_kilogram_metres_per_second(
            result->particle_impulses[0].internal_impulse[0] +
            result->particle_impulses[1].internal_impulse[0] +
            result->particle_impulses[2].internal_impulse[0]),
        0.0,
        0.0,
        "all pair impulses must be equal/opposite in the explicit ledger");
    require_near(units::in_joules(result->unresolved_internal_energy_flux), 0.0, 1.0e-12,
                 "elastic sequential contact must retain kinetic energy");

    const auto repeat = solvers::resolve_frictionless_contact_system_drift(
        {shuffled[1], shuffled[2], shuffled[0]},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(2.0));
    require(repeat.has_value() && *repeat == *result,
            "input permutation must not change a bitwise-comparable canonical result");
}

void test_multiple_boundary_events_and_budget()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    auto context = make_context();
    add_wall(context, 20, -3.0, -5.0, -2.0, 5.0);
    add_wall(context, 10, 2.0, -5.0, 3.0, 5.0);
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 1.0, 10.0, 0.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(0.8));
    require(result.has_value() && result->contacts.size() == 3,
            "bounded event-driven drift must process sequential wall bounces");
    require_near(units::in_seconds(result->contacts[0].time_from_start), 0.15, 1.0e-12,
                 "first wall impact time must be exact");
    require_near(units::in_seconds(result->contacts[1].time_from_start), 0.45, 1.0e-12,
                 "second wall impact time must be exact");
    require_near(units::in_seconds(result->contacts[2].time_from_start), 0.75, 1.0e-12,
                 "third wall impact time must be exact");
    require_near(units::in_metres(result->bodies.front().state.position[0]), 1.0, 1.0e-12,
                 "all remaining drift intervals must be applied without tunneling");
    require_near(
        units::in_kilogram_metres_per_second(result->particle_impulses.front().boundary_impulse[0]),
        -20.0,
        1.0e-12,
        "per-particle boundary ledger must equal the net external impulse");
    require_near(
        units::in_kilogram_square_metres_per_second(result->boundary_angular_impulse),
        20.0,
        1.0e-12,
        "boundary angular impulse must be accumulated about the world origin");
    const auto final_momentum_x =
        units::in_kilogram_metres_per_second(result->bodies.front().state.momentum[0]);
    const auto final_angular_momentum =
        units::in_metres(result->bodies.front().state.position[0]) *
            units::in_kilogram_metres_per_second(result->bodies.front().state.momentum[1]) -
        units::in_metres(result->bodies.front().state.position[1]) * final_momentum_x;
    require_near(
        final_momentum_x - 10.0,
        units::in_kilogram_metres_per_second(result->boundary_impulse[0]),
        1.0e-12,
        "final-minus-initial momentum must equal the explicit boundary impulse");
    require_near(
        final_angular_momentum - (-10.0),
        units::in_kilogram_square_metres_per_second(result->boundary_angular_impulse),
        1.0e-12,
        "final-minus-initial orbital angular momentum must equal the boundary angular impulse");

    const auto limited = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 1.0, 10.0, 0.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(0.8),
        solvers::FrictionlessContactSystemConfiguration{2, 1024, 1024, 1.0e-12, 1.0e-12});
    require(!limited && limited.error() == solvers::FrictionlessContactSystemError::EventLimitExceeded,
            "the global event budget must fail explicitly before unbounded work");
}

void test_exact_rounded_corner_sweep()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    auto context = make_context();
    add_wall(context, 1, 5.0, 5.0, 6.0, 6.0);
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, 10.0, 10.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(result.has_value() && result->contacts.size() == 1,
            "a fast diagonal disc must hit the rounded Minkowski corner instead of tunneling");
    const auto expected_time = (5.0 - 0.5 / std::sqrt(2.0)) / 10.0;
    require_near(units::in_seconds(result->contacts.front().time_from_start), expected_time, 1.0e-12,
                 "corner time of impact must use the analytic circle root");
    require_near(units::in_metres(result->contacts.front().contact_point[0]), 5.0, 1.0e-12,
                 "corner contact point x must lie on the actual boundary corner");
    require_near(units::in_metres(result->contacts.front().contact_point[1]), 5.0, 1.0e-12,
                 "corner contact point y must lie on the actual boundary corner");
    require_near(units::in_metres(result->bodies.front().state.position[0]), -1.0 / std::sqrt(2.0), 1.0e-11,
                 "post-corner reflection must consume the exact remaining drift time");
    require_near(units::in_metres(result->bodies.front().state.position[1]), -1.0 / std::sqrt(2.0), 1.0e-11,
                 "rounded-corner normal must reflect both momentum components");
}

void test_canonical_independent_simultaneity()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {
            body(4, 12.0, 0.0, 0.0, 0.0),
            body(2, 2.0, 0.0, 0.0, 0.0),
            body(3, 10.0, 0.0, 2.0, 0.0),
            body(1, 0.0, 0.0, 2.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(result.has_value() && result->contacts.size() == 2,
            "independent simultaneous pairs are supported without order-biased coupling");
    require_near(units::in_seconds(result->contacts[0].time_from_start), 0.5, 1.0e-12,
                 "first independent event must retain its common physical time");
    require_near(units::in_seconds(result->contacts[1].time_from_start), 0.5, 1.0e-12,
                 "second independent event must retain its common physical time");
    require(result->contacts[0].first_particle == world::ParticleId{1} &&
                result->contacts[1].first_particle == world::ParticleId{3},
            "equal-time equal-kind events must order by persistent particle IDs");

    auto mixed_context = make_context();
    add_wall(mixed_context, 1, 2.0, -2.0, 3.0, 2.0);
    const auto mixed = solvers::resolve_frictionless_contact_system_drift(
        {
            body(9, 0.0, 0.0, 3.0, 0.0),
            body(2, 12.0, 0.0, 0.0, 0.0),
            body(1, 10.0, 0.0, 2.0, 0.0),
        },
        mixed_context.boundaries,
        mixed_context.materials,
        mixed_context.responses,
        units::seconds(1.0));
    require(mixed.has_value() && mixed->contacts.size() == 2,
            "independent wall and pair events may occur at one exact time");
    require(mixed->contacts[0].kind ==
                solvers::FrictionlessContactSystemEventKind::StaticBoundary &&
                mixed->contacts[1].kind ==
                    solvers::FrictionlessContactSystemEventKind::ParticlePair,
            "equal-time event selection must order by the declared event-kind contract before IDs");
    require_near(units::in_seconds(mixed->contacts[0].time_from_start), 0.5, 1.0e-12,
                 "wall member of a mixed tie must retain the exact event time");
    require_near(units::in_seconds(mixed->contacts[1].time_from_start), 0.5, 1.0e-12,
                 "pair member of a mixed tie must retain the exact event time");
}

void test_coupled_simultaneity_is_rejected()
{
    using namespace principia;
    using tests::require;

    const auto context = make_context();
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {
            body(3, 2.0, 0.0, -1.0, 0.0),
            body(2, 0.0, 0.0, 0.0, 0.0),
            body(1, -2.0, 0.0, 1.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(2.0));
    require(!result &&
                result.error() ==
                    solvers::FrictionlessContactSystemError::UnsupportedCoupledSimultaneousContact,
            "two same-time contacts sharing a disc must not be resolved by arbitrary pair order");

    auto mixed_context = make_context();
    add_wall(mixed_context, 1, 2.0, -2.0, 3.0, 2.0);
    const auto mixed = solvers::resolve_frictionless_contact_system_drift(
        {
            body(1, 0.0, 0.0, 1.0, 0.0),
            body(2, -4.0, 0.0, 3.0, 0.0),
        },
        mixed_context.boundaries,
        mixed_context.materials,
        mixed_context.responses,
        units::seconds(2.0));
    require(!mixed &&
                mixed.error() ==
                    solvers::FrictionlessContactSystemError::UnsupportedCoupledSimultaneousContact,
            "same-time wall and pair contact sharing a disc must be rejected as coupled");

    const auto resting_chain = solvers::resolve_frictionless_contact_system_drift(
        {
            body(1, 0.0, 0.0, 1.0, 0.0),
            body(2, 1.0, 0.0, 0.0, 0.0),
            body(3, 2.0, 0.0, 0.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(!resting_chain &&
                resting_chain.error() ==
                    solvers::FrictionlessContactSystemError::UnsupportedCoupledSimultaneousContact,
            "a resting touching neighbor in an impact network must be detected before impulse propagation");
}

void test_dissipation_and_equal_opposite_pair_impulse()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {
            body(2, 2.0, 0.0, 0.0, 0.0, 3.0, 2),
            body(1, 0.0, 0.0, 2.0, 0.0, 1.0, 2),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(result.has_value() && result->contacts.size() == 1,
            "a dissipative unequal-mass pair must resolve analytically");
    require_near(units::in_joules(result->contacts.front().energy_to_unresolved_internal_modes),
                 1.125, 1.0e-12,
                 "event record must expose the exact restitution loss");
    require_near(units::in_joules(result->unresolved_internal_energy_flux), 1.125, 1.0e-12,
                 "system energy flux must equal the sum of unresolved event losses");
    require_near(
        units::in_kilogram_metres_per_second(
            result->particle_impulses[0].internal_impulse[0] +
            result->particle_impulses[1].internal_impulse[0]),
        0.0,
        0.0,
        "dissipative pair impulses must remain exactly equal/opposite");

    const auto sub_tolerance_gap = solvers::resolve_frictionless_contact_system_drift(
        {
            body(1, 0.0, 0.0, 1.0, 0.0),
            body(2, 1.0000000000005, 0.0, 0.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(sub_tolerance_gap.has_value() && sub_tolerance_gap->contacts.size() == 1,
            "a positive gap smaller than geometric tolerance must still resolve");
    require(units::in_seconds(sub_tolerance_gap->contacts.front().time_from_start) > 0.0,
            "geometric tolerance must not snap a separated pair into an early time-zero impact");
    require_near(
        units::in_seconds(sub_tolerance_gap->contacts.front().time_from_start),
        5.000444502910455e-13,
        1.0e-16,
                 "sub-tolerance pair contact must retain its stable analytic root");
}

void test_long_baseline_near_grazing_toi()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    constexpr auto baseline = 100'000'000.0;
    constexpr auto impact_parameter = 0.99;
    const auto expected_time = baseline - std::sqrt(1.0 - impact_parameter * impact_parameter);
    const auto pair = solvers::resolve_frictionless_contact_system_drift(
        {
            body(2, baseline, impact_parameter, 0.0, 0.0),
            body(1, 0.0, 0.0, 1.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(baseline));
    if (!pair) {
        throw std::runtime_error(
            "long-baseline pair failed with contact-system error " +
            std::to_string(static_cast<unsigned>(pair.error())));
    }
    require(pair.has_value() && pair->contacts.size() == 1,
            "a long-baseline near-grazing pair must not lose its impact parameter in a discriminant");
    require_near(units::in_seconds(pair->contacts.front().time_from_start),
                 expected_time, 5.0e-8,
                 "velocity-aligned pair TOI must retain the near-grazing circle root");
    const auto pair_normal_magnitude = std::hypot(
        pair->contacts.front().normal_from_second_to_first[0],
        pair->contacts.front().normal_from_second_to_first[1]);
    require_near(pair_normal_magnitude, 1.0, 1.0e-14,
                 "long-baseline pair root must reconstruct a unit contact normal");
    const auto directly_reconstructed_pair_distance = std::hypot(
        baseline - units::in_seconds(pair->contacts.front().time_from_start),
        impact_parameter);
    require_near(directly_reconstructed_pair_distance, 1.0, 5.0e-8,
                 "accepted long-baseline pair TOI must lie on the contact manifold");
    const auto pair_reordered = solvers::resolve_frictionless_contact_system_drift(
        {
            body(1, 0.0, 0.0, 1.0, 0.0),
            body(2, baseline, impact_parameter, 0.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(baseline));
    require(pair_reordered.has_value() && *pair_reordered == *pair,
            "stable long-baseline contact must remain bitwise deterministic under input permutation");

    auto corner_context = make_context();
    add_wall(corner_context, 1, baseline, 0.0, baseline + 1.0, 1.0);
    const auto corner = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, -impact_parameter, 1.0, 0.0, 1.0, 1, 1.0)},
        corner_context.boundaries,
        corner_context.materials,
        corner_context.responses,
        units::seconds(baseline));
    if (!corner) {
        throw std::runtime_error(
            "long-baseline corner failed with contact-system error " +
            std::to_string(static_cast<unsigned>(corner.error())));
    }
    require(corner.has_value() && corner->contacts.size() == 1,
            "a long-baseline rounded corner must use the stable circle sweep rather than a squared discriminant");
    require_near(units::in_seconds(corner->contacts.front().time_from_start),
                 expected_time, 5.0e-8,
                 "rounded-corner TOI must retain its long-baseline impact parameter");
    require_near(units::in_metres(corner->contacts.front().contact_point[0]),
                 baseline, 5.0e-8,
                 "verified corner event must reconstruct onto the boundary corner x coordinate");
    require_near(units::in_metres(corner->contacts.front().contact_point[1]),
                 0.0, 5.0e-8,
                 "verified corner event must reconstruct onto the boundary corner y coordinate");
}

void test_strict_time_horizon()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    constexpr auto speed = 1'000'000'000.0;
    constexpr auto gap_beyond_horizon = 0.0005;
    const auto center_separation = speed + 1.0 + gap_beyond_horizon;
    const auto pair = solvers::resolve_frictionless_contact_system_drift(
        {
            body(1, 0.0, 0.0, speed, 0.0),
            body(2, center_separation, 0.0, 0.0, 0.0),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(pair.has_value() && pair->contacts.empty(),
            "a pair impact after dt must not be pulled into the step by simultaneity tolerance");
    require_near(units::in_kilogram_metres_per_second(pair->bodies[0].state.momentum[0]),
                 speed, 0.0,
                 "future pair contact must not alter endpoint momentum");
    require_near(
        units::in_metres(pair->bodies[1].state.position[0] - pair->bodies[0].state.position[0]),
        1.0 + gap_beyond_horizon,
        2.0e-7,
        "future pair contact must retain the positive endpoint gap");

    auto boundary_context = make_context();
    add_wall(
        boundary_context,
        1,
        speed + 0.5 + gap_beyond_horizon,
        -1.0,
        speed + 1.5 + gap_beyond_horizon,
        1.0);
    const auto boundary = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, speed, 0.0)},
        boundary_context.boundaries,
        boundary_context.materials,
        boundary_context.responses,
        units::seconds(1.0));
    require(boundary.has_value() && boundary->contacts.empty(),
            "a boundary impact after dt must not be clamped onto the horizon");
    require_near(units::in_kilogram_metres_per_second(boundary->bodies.front().state.momentum[0]),
                 speed, 0.0,
                 "future boundary contact must not alter endpoint momentum");
}

void test_dissipation_with_huge_tangential_energy()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto context = make_context();
    constexpr auto common_tangential_velocity = 100'000'000.0;
    const auto result = solvers::resolve_frictionless_contact_system_drift(
        {
            body(2, 2.0, 0.0, 0.0, 3.0 * common_tangential_velocity, 3.0, 2),
            body(1, 0.0, 0.0, 2.0, common_tangential_velocity, 1.0, 2),
        },
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(result.has_value() && result->contacts.size() == 1,
            "large common tangential energy must not obscure a normal pair collision");
    require_near(units::in_joules(result->contacts.front().energy_to_unresolved_internal_modes),
                 1.125, 1.0e-12,
                 "restitution loss must come from the analytic normal-energy law, not total-KE subtraction");
    require_near(units::in_joules(result->unresolved_internal_energy_flux),
                 1.125, 1.0e-12,
                 "aggregate unresolved flux must retain loss far below the total-KE floating-point ulp");
    require_near(
        units::in_kilogram_metres_per_second(result->bodies[0].state.momentum[1]),
        common_tangential_velocity,
        0.0,
        "frictionless contact must leave first-body tangential momentum unchanged");
    require_near(
        units::in_kilogram_metres_per_second(result->bodies[1].state.momentum[1]),
        3.0 * common_tangential_velocity,
        0.0,
        "frictionless contact must leave second-body tangential momentum unchanged");
}

void test_invalid_states_and_resource_limits()
{
    using namespace principia;
    using tests::require;

    const auto context = make_context();
    const auto pair_overlap = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, 0.0, 0.0), body(2, 0.5, 0.0, 0.0, 0.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(!pair_overlap && pair_overlap.error() == solvers::FrictionlessContactSystemError::InitialOverlap,
            "initial pair penetration must fail before mutation");

    auto wall_context = make_context();
    add_wall(wall_context, 1, 5.0, -1.0, 6.0, 1.0);
    const auto wall_overlap = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 5.25, 0.0, 0.0, 0.0)},
        wall_context.boundaries,
        wall_context.materials,
        wall_context.responses,
        units::seconds(1.0));
    require(!wall_overlap && wall_overlap.error() == solvers::FrictionlessContactSystemError::InitialOverlap,
            "initial boundary penetration must fail explicitly");

    const auto duplicate = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, 0.0, 0.0), body(1, 2.0, 0.0, 0.0, 0.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0));
    require(!duplicate && duplicate.error() == solvers::FrictionlessContactSystemError::DuplicateParticle,
            "duplicate persistent IDs must be rejected after canonical sorting");

    auto unidentified = body(1, 0.0, 0.0, 0.0, 0.0);
    unidentified.state.id = world::ParticleId{};
    const auto invalid_identifier = solvers::resolve_frictionless_contact_system_drift(
        {unidentified}, context.boundaries, context.materials, context.responses, units::seconds(1.0));
    require(!invalid_identifier &&
                invalid_identifier.error() == solvers::FrictionlessContactSystemError::InvalidIdentifier,
            "reserved persistent identifiers must fail distinctly from numeric input errors");

    auto constrained = body(1, 0.0, 0.0, 0.0, 0.0);
    constrained.state.constraint = world::KinematicConstraint::Fixed;
    const auto fixed = solvers::resolve_frictionless_contact_system_drift(
        {constrained}, context.boundaries, context.materials, context.responses, units::seconds(1.0));
    require(!fixed && fixed.error() == solvers::FrictionlessContactSystemError::InvalidConstraint,
            "the free-disc solver must reject constrained bodies");

    auto nonfinite = body(1, 0.0, 0.0, 0.0, 0.0);
    nonfinite.state.position[0] = units::metres(std::numeric_limits<double>::quiet_NaN());
    const auto nan = solvers::resolve_frictionless_contact_system_drift(
        {nonfinite}, context.boundaries, context.materials, context.responses, units::seconds(1.0));
    require(!nan && nan.error() == solvers::FrictionlessContactSystemError::NonFiniteInput,
            "nonfinite canonical state must be rejected");

    const auto invalid_dt = solvers::resolve_frictionless_contact_system_drift(
        {}, context.boundaries, context.materials, context.responses, units::seconds(0.0));
    require(!invalid_dt && invalid_dt.error() == solvers::FrictionlessContactSystemError::NonPositiveTimeStep,
            "non-positive global drift duration must be rejected");

    const auto body_limited = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, 0.0, 0.0), body(2, 2.0, 0.0, 0.0, 0.0)},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(1.0),
        solvers::FrictionlessContactSystemConfiguration{64, 1, 1024, 1.0e-12, 1.0e-12});
    require(!body_limited && body_limited.error() == solvers::FrictionlessContactSystemError::BodyLimitExceeded,
            "body resource bound must be checked before quadratic candidate work");

    auto boundary_context = make_context();
    add_wall(boundary_context, 1, 5.0, -1.0, 6.0, 1.0);
    add_wall(boundary_context, 2, 8.0, -1.0, 9.0, 1.0);
    const auto boundary_limited = solvers::resolve_frictionless_contact_system_drift(
        {},
        boundary_context.boundaries,
        boundary_context.materials,
        boundary_context.responses,
        units::seconds(1.0),
        solvers::FrictionlessContactSystemConfiguration{64, 1024, 1, 1.0e-12, 1.0e-12});
    require(!boundary_limited &&
                boundary_limited.error() ==
                    solvers::FrictionlessContactSystemError::BoundaryLimitExceeded,
            "boundary resource bound must be checked before body-boundary candidate work");

    materials::MaterialRegistry incomplete_materials;
    require(incomplete_materials.try_add(material(1, 99)).has_value(),
            "incomplete-registry fixture must contain a syntactically valid material");
    const auto incomplete = solvers::resolve_frictionless_contact_system_drift(
        {body(1, 0.0, 0.0, 0.0, 0.0)},
        context.boundaries,
        incomplete_materials,
        context.responses,
        units::seconds(1.0));
    require(!incomplete &&
                incomplete.error() == solvers::FrictionlessContactSystemError::MissingMechanicalResponse,
            "cross-registry mechanical references must resolve before contact work");

    auto overflow = body(
        1,
        std::numeric_limits<double>::max(),
        0.0,
        std::numeric_limits<double>::max(),
        0.0);
    const auto overflowed = solvers::resolve_frictionless_contact_system_drift(
        {overflow},
        context.boundaries,
        context.materials,
        context.responses,
        units::seconds(std::numeric_limits<double>::max()));
    require(!overflowed && overflowed.error() == solvers::FrictionlessContactSystemError::NumericalOverflow,
            "finite inputs whose drift arithmetic overflows must fail explicitly");
}

}  // namespace

int main()
{
    try {
        test_sequential_pair_events_and_internal_ledgers();
        test_multiple_boundary_events_and_budget();
        test_exact_rounded_corner_sweep();
        test_canonical_independent_simultaneity();
        test_coupled_simultaneity_is_rejected();
        test_dissipation_and_equal_opposite_pair_impulse();
        test_long_baseline_near_grazing_toi();
        test_strict_time_horizon();
        test_dissipation_with_huge_tangential_energy();
        test_invalid_states_and_resource_limits();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
