#include <principia/solvers/frictionless_contact.hpp>
#include <principia/solvers/frictionless_contact_system.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace principia::solvers {

namespace {

[[nodiscard]] bool finite_body(const ContactBody2& body)
{
    return body.state.id && body.state.material &&
           std::isfinite(units::in_metres(body.state.position[0])) &&
           std::isfinite(units::in_metres(body.state.position[1])) &&
           std::isfinite(units::in_kilogram_metres_per_second(body.state.momentum[0])) &&
           std::isfinite(units::in_kilogram_metres_per_second(body.state.momentum[1])) &&
           std::isfinite(units::in_kilograms(body.state.rest_mass)) &&
           std::isfinite(units::in_metres(body.radius));
}

[[nodiscard]] std::expected<void, FrictionlessContactError> validate_body_and_step(
    const ContactBody2& body,
    units::Duration dt)
{
    const auto seconds = units::in_seconds(dt);
    if (!std::isfinite(seconds)) {
        return std::unexpected(FrictionlessContactError::NonFiniteInput);
    }
    if (seconds <= 0.0) {
        return std::unexpected(FrictionlessContactError::NonPositiveTimeStep);
    }
    if (!finite_body(body)) {
        return std::unexpected(FrictionlessContactError::NonFiniteInput);
    }
    if (units::in_kilograms(body.state.rest_mass) <= 0.0) {
        return std::unexpected(FrictionlessContactError::NonPositiveMass);
    }
    if (units::in_metres(body.radius) <= 0.0) {
        return std::unexpected(FrictionlessContactError::NonPositiveRadius);
    }
    if (body.state.constraint != world::KinematicConstraint::Free) {
        return std::unexpected(FrictionlessContactError::InvalidConstraint);
    }
    return {};
}

[[nodiscard]] FrictionlessContactError map_system_error(FrictionlessContactSystemError error)
{
    switch (error) {
    case FrictionlessContactSystemError::NonPositiveTimeStep:
        return FrictionlessContactError::NonPositiveTimeStep;
    case FrictionlessContactSystemError::NonFiniteInput:
        return FrictionlessContactError::NonFiniteInput;
    case FrictionlessContactSystemError::NonPositiveMass:
        return FrictionlessContactError::NonPositiveMass;
    case FrictionlessContactSystemError::NonPositiveRadius:
        return FrictionlessContactError::NonPositiveRadius;
    case FrictionlessContactSystemError::InvalidConstraint:
        return FrictionlessContactError::InvalidConstraint;
    case FrictionlessContactSystemError::MissingMaterial:
        return FrictionlessContactError::MissingMaterial;
    case FrictionlessContactSystemError::MissingMechanicalResponse:
        return FrictionlessContactError::MissingMechanicalResponse;
    case FrictionlessContactSystemError::UnsupportedMechanicalResponse:
        return FrictionlessContactError::UnsupportedMechanicalResponse;
    case FrictionlessContactSystemError::InvalidConfiguration:
    case FrictionlessContactSystemError::BodyLimitExceeded:
    case FrictionlessContactSystemError::BoundaryLimitExceeded:
    case FrictionlessContactSystemError::InvalidBoundaryRegistry:
    case FrictionlessContactSystemError::InvalidMaterialRegistry:
    case FrictionlessContactSystemError::InvalidMechanicalResponseRegistry:
        return FrictionlessContactError::InvalidConfiguration;
    case FrictionlessContactSystemError::InvalidIdentifier:
        return FrictionlessContactError::NonFiniteInput;
    case FrictionlessContactSystemError::DuplicateParticle:
        return FrictionlessContactError::DuplicateParticle;
    case FrictionlessContactSystemError::InitialOverlap:
        return FrictionlessContactError::InitialOverlap;
    case FrictionlessContactSystemError::UnsupportedCoupledSimultaneousContact:
        return FrictionlessContactError::UnsupportedCoupledSimultaneousContact;
    case FrictionlessContactSystemError::EventLimitExceeded:
        return FrictionlessContactError::EventLimitExceeded;
    case FrictionlessContactSystemError::NumericalOverflow:
        return FrictionlessContactError::NumericalOverflow;
    }
    return FrictionlessContactError::NumericalOverflow;
}

}  // namespace

std::expected<ContactDriftResult2, FrictionlessContactError> resolve_disc_against_static_boundaries(
    ContactBody2 body,
    const boundaries::BoundaryRegistry& boundary_registry,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    FrictionlessContactConfiguration configuration)
{
    const auto body_validity = validate_body_and_step(body, dt);
    if (!body_validity) {
        return std::unexpected(body_validity.error());
    }
    if (!std::isfinite(configuration.geometric_tolerance_metres) ||
        !std::isfinite(configuration.time_fraction_tolerance) ||
        configuration.geometric_tolerance_metres < 0.0 || configuration.time_fraction_tolerance < 0.0) {
        return std::unexpected(FrictionlessContactError::InvalidConfiguration);
    }
    const auto simultaneous_tolerance_seconds =
        configuration.time_fraction_tolerance * units::in_seconds(dt);
    if (!std::isfinite(simultaneous_tolerance_seconds)) {
        return std::unexpected(FrictionlessContactError::InvalidConfiguration);
    }
    auto system_result = resolve_frictionless_contact_system_drift(
        std::vector<ContactBody2>{body},
        boundary_registry,
        materials,
        mechanical_responses,
        dt,
        FrictionlessContactSystemConfiguration{
            configuration.maximum_boundary_events,
            1,
            std::max<std::size_t>(1, boundary_registry.size()),
            configuration.geometric_tolerance_metres,
            simultaneous_tolerance_seconds,
        });
    if (!system_result) {
        return std::unexpected(map_system_error(system_result.error()));
    }
    if (system_result->bodies.size() != 1 || system_result->particle_impulses.size() != 1 ||
        system_result->bodies.front().state.id != body.state.id ||
        system_result->particle_impulses.front().particle != body.state.id) {
        return std::unexpected(FrictionlessContactError::NumericalOverflow);
    }

    ContactDriftResult2 result{
        system_result->bodies.front(),
        system_result->particle_impulses.front().boundary_impulse,
        system_result->unresolved_internal_energy_flux,
        {},
    };
    result.contacts.reserve(system_result->contacts.size());
    for (const auto& event : system_result->contacts) {
        if (event.kind != FrictionlessContactSystemEventKind::StaticBoundary ||
            !event.boundary || event.second_particle) {
            return std::unexpected(FrictionlessContactError::NumericalOverflow);
        }
        const auto center_at_contact = event.contact_point + math::Vector<2, units::Length>{
            body.radius * event.normal_from_second_to_first[0],
            body.radius * event.normal_from_second_to_first[1],
        };
        result.contacts.push_back(FrictionlessContactEvent2{
            event.time_from_start,
            center_at_contact,
            event.normal_from_second_to_first,
            event.impulse_on_first,
            std::nullopt,
            event.boundary,
            event.energy_to_unresolved_internal_modes,
        });
    }
    return result;
}

std::expected<PairContactDriftResult2, FrictionlessContactError> resolve_disc_pair_drift(
    ContactBody2 left,
    ContactBody2 right,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    double geometric_tolerance_metres)
{
    if (right.state.id < left.state.id) {
        std::swap(left, right);
    }
    if (left.state.id == right.state.id) {
        return std::unexpected(FrictionlessContactError::DuplicateParticle);
    }
    const auto left_validity = validate_body_and_step(left, dt);
    const auto right_validity = validate_body_and_step(right, dt);
    if (!left_validity) {
        return std::unexpected(left_validity.error());
    }
    if (!right_validity) {
        return std::unexpected(right_validity.error());
    }
    if (!std::isfinite(geometric_tolerance_metres) || geometric_tolerance_metres < 0.0) {
        return std::unexpected(FrictionlessContactError::InvalidConfiguration);
    }

    const boundaries::BoundaryRegistry no_boundaries;
    auto system_result = resolve_frictionless_contact_system_drift(
        std::vector<ContactBody2>{left, right},
        no_boundaries,
        materials,
        mechanical_responses,
        dt,
        FrictionlessContactSystemConfiguration{
            1,
            2,
            1,
            geometric_tolerance_metres,
            0.0,
        });
    if (!system_result) {
        return std::unexpected(map_system_error(system_result.error()));
    }
    if (system_result->bodies.size() != 2 || system_result->particle_impulses.size() != 2 ||
        system_result->bodies[0].state.id != left.state.id ||
        system_result->bodies[1].state.id != right.state.id ||
        system_result->contacts.size() > 1) {
        return std::unexpected(FrictionlessContactError::NumericalOverflow);
    }

    std::optional<FrictionlessContactEvent2> contact;
    if (!system_result->contacts.empty()) {
        const auto& event = system_result->contacts.front();
        if (event.kind != FrictionlessContactSystemEventKind::ParticlePair ||
            event.first_particle != left.state.id || event.second_particle != right.state.id ||
            event.boundary) {
            return std::unexpected(FrictionlessContactError::NumericalOverflow);
        }
        const auto center_at_contact = event.contact_point + math::Vector<2, units::Length>{
            left.radius * event.normal_from_second_to_first[0],
            left.radius * event.normal_from_second_to_first[1],
        };
        contact = FrictionlessContactEvent2{
            event.time_from_start,
            center_at_contact,
            event.normal_from_second_to_first,
            event.impulse_on_first,
            event.second_particle,
            std::nullopt,
            event.energy_to_unresolved_internal_modes,
        };
    }
    return PairContactDriftResult2{
        system_result->bodies[0],
        system_result->bodies[1],
        std::move(contact),
    };
}

}  // namespace principia::solvers
