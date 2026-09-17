#include <principia/solvers/newtonian_particle_solver.hpp>

#include <principia/ontology/theory.hpp>
#include <principia/state/channel.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace principia::solvers {

namespace {

[[nodiscard]] math::Vector<2, units::Momentum> zero_momentum_vector()
{
    return math::Vector<2, units::Momentum>{
        units::kilogram_metres_per_second(0.0),
        units::kilogram_metres_per_second(0.0),
    };
}

[[nodiscard]] bool finite_position(const spacetime::WorldPosition<2>& position)
{
    return std::isfinite(units::in_metres(position[0])) && std::isfinite(units::in_metres(position[1]));
}

[[nodiscard]] bool finite_momentum(const math::Vector<2, units::Momentum>& momentum)
{
    return std::isfinite(units::in_kilogram_metres_per_second(momentum[0])) &&
           std::isfinite(units::in_kilogram_metres_per_second(momentum[1]));
}

[[nodiscard]] bool finite_gravity(const fields::GravityVector<2>& gravity)
{
    return std::isfinite(units::in_metres_per_second_squared(gravity[0])) &&
           std::isfinite(units::in_metres_per_second_squared(gravity[1]));
}

[[nodiscard]] bool zero_momentum(const math::Vector<2, units::Momentum>& momentum)
{
    return units::in_kilogram_metres_per_second(momentum[0]) == 0.0 &&
           units::in_kilogram_metres_per_second(momentum[1]) == 0.0;
}

[[nodiscard]] units::AngularMomentum angular_momentum_about_origin(
    const spacetime::WorldPosition<2>& position,
    const math::Vector<2, units::Momentum>& momentum)
{
    return position[0] * momentum[1] - position[1] * momentum[0];
}

[[nodiscard]] units::Energy kinetic_energy(
    const math::Vector<2, units::Momentum>& momentum,
    units::Mass mass)
{
    return math::dot(momentum, momentum) / (mass * 2.0);
}

[[nodiscard]] units::Energy impulse_work(
    const math::Vector<2, units::Momentum>& momentum_before,
    const math::Vector<2, units::Momentum>& momentum_after,
    const math::Vector<2, units::Momentum>& impulse,
    units::Mass mass)
{
    const auto average_velocity = (momentum_before + momentum_after) / (mass * 2.0);
    return math::dot(average_velocity, impulse);
}

[[nodiscard]] bool any_exchange(const math::Vector<2, units::Momentum>& value)
{
    return !zero_momentum(value);
}

[[nodiscard]] MechanicsSourceStamp2 source_stamp(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2* contact,
    NewtonianValidityConfiguration validity_configuration)
{
    if (contact == nullptr) {
        return MechanicsSourceStamp2{
            world.particles.revision(),
            gravity.revision(),
            0,
            0,
            0,
            0,
            world.tick,
            world.simulation_time,
            validity_configuration,
            false,
            {},
        };
    }
    return MechanicsSourceStamp2{
        world.particles.revision(),
        gravity.revision(),
        world.boundaries.revision(),
        contact->materials.revision(),
        contact->mechanical_responses.revision(),
        contact->disc_colliders.revision(),
        world.tick,
        world.simulation_time,
        validity_configuration,
        true,
        contact->configuration,
    };
}

[[nodiscard]] bool valid_validity_configuration(const NewtonianValidityConfiguration& configuration)
{
    const auto invariant_speed = units::in_metres_per_second(configuration.invariant_speed);
    return std::isfinite(invariant_speed) && invariant_speed > 0.0 &&
           std::isfinite(configuration.maximum_beta) && configuration.maximum_beta > 0.0;
}

[[nodiscard]] bool valid_contact_configuration(const FrictionlessContactSystemConfiguration& configuration)
{
    return std::isfinite(configuration.geometric_tolerance_metres) &&
           std::isfinite(configuration.simultaneous_time_tolerance_seconds) &&
           configuration.geometric_tolerance_metres >= 0.0 &&
           configuration.simultaneous_time_tolerance_seconds >= 0.0 &&
           configuration.maximum_bodies > 0 && configuration.maximum_boundaries > 0;
}

[[nodiscard]] std::expected<void, NewtonianStepError> validate_contact_environment(
    const world::WorldState2& world,
    const NewtonianContactEnvironment2& contact)
{
    if (!valid_contact_configuration(contact.configuration)) {
        return std::unexpected(NewtonianStepError::InvalidContactEnvironment);
    }
    if (contact.disc_colliders.size() > contact.configuration.maximum_bodies ||
        world.boundaries.size() > contact.configuration.maximum_boundaries) {
        return std::unexpected(NewtonianStepError::InvalidContactEnvironment);
    }
    for (const auto& [particle_id, collider] : contact.disc_colliders.ordered_colliders()) {
        if (particle_id != collider.particle || !validate_disc_collider(collider)) {
            return std::unexpected(NewtonianStepError::InvalidContactEnvironment);
        }
        const auto* particle = world.particles.find(particle_id);
        if (particle == nullptr || particle->constraint != world::KinematicConstraint::Free) {
            return std::unexpected(NewtonianStepError::InvalidContactEnvironment);
        }
        const auto* material = contact.materials.find(particle->material);
        if (material == nullptr || contact.mechanical_responses.find(material->mechanical) == nullptr) {
            return std::unexpected(NewtonianStepError::InvalidContactEnvironment);
        }
    }
    return {};
}

[[nodiscard]] NewtonianStepError map_contact_error(FrictionlessContactSystemError error)
{
    switch (error) {
    case FrictionlessContactSystemError::NonPositiveTimeStep:
        return NewtonianStepError::NonPositiveTimeStep;
    case FrictionlessContactSystemError::NonFiniteInput:
    case FrictionlessContactSystemError::NumericalOverflow:
        return NewtonianStepError::NumericalOverflow;
    case FrictionlessContactSystemError::NonPositiveMass:
        return NewtonianStepError::NonPositiveMass;
    case FrictionlessContactSystemError::InvalidConstraint:
        return NewtonianStepError::InvalidConstraint;
    case FrictionlessContactSystemError::InitialOverlap:
    case FrictionlessContactSystemError::UnsupportedCoupledSimultaneousContact:
    case FrictionlessContactSystemError::EventLimitExceeded:
        return NewtonianStepError::ContactResolutionFailed;
    case FrictionlessContactSystemError::NonPositiveRadius:
    case FrictionlessContactSystemError::InvalidConfiguration:
    case FrictionlessContactSystemError::BodyLimitExceeded:
    case FrictionlessContactSystemError::BoundaryLimitExceeded:
    case FrictionlessContactSystemError::InvalidIdentifier:
    case FrictionlessContactSystemError::DuplicateParticle:
    case FrictionlessContactSystemError::InvalidBoundaryRegistry:
    case FrictionlessContactSystemError::InvalidMaterialRegistry:
    case FrictionlessContactSystemError::InvalidMechanicalResponseRegistry:
    case FrictionlessContactSystemError::MissingMaterial:
    case FrictionlessContactSystemError::MissingMechanicalResponse:
    case FrictionlessContactSystemError::UnsupportedMechanicalResponse:
        return NewtonianStepError::InvalidContactEnvironment;
    }
    return NewtonianStepError::ContactResolutionFailed;
}

struct KdkParticleResult {
    spacetime::WorldPosition<2> position;
    math::Vector<2, units::Momentum> momentum;
    fields::GravityVector<2> initial_gravity;
    fields::GravityVector<2> final_gravity;
    math::Vector<2, units::Momentum> initial_gravity_impulse;
    math::Vector<2, units::Momentum> final_gravity_impulse;
    math::Vector<2, units::Momentum> momentum_after_contact_drift;
    math::Vector<2, units::Momentum> internal_contact_impulse;
    math::Vector<2, units::Momentum> constraint_reaction_impulse;
    math::Vector<2, units::Momentum> boundary_impulse;
    units::AngularMomentum boundary_angular_impulse;
};

[[nodiscard]] std::expected<KdkParticleResult, NewtonianStepError> integrate_kick_drift_kick(
    const world::ParticleState2& particle,
    const fields::EffectiveGravityField<2>& gravity,
    spacetime::SimulationTime time,
    units::Duration dt)
{
    const auto half_dt = dt / 2.0;
    const spacetime::Event<2> initial_event{time, particle.position, spacetime::FrameId{1}};
    const auto initial_gravity = gravity.sample(initial_event);
    if (!finite_gravity(initial_gravity)) {
        return std::unexpected(NewtonianStepError::NonFiniteFieldSample);
    }
    const math::Vector<2, units::Momentum> initial_impulse =
        initial_gravity * particle.rest_mass * half_dt;
    const math::Vector<2, units::Momentum> momentum_after_initial_kick =
        particle.momentum + initial_impulse;
    const auto position = particle.constraint == world::KinematicConstraint::Fixed
                              ? particle.position
                              : particle.position +
                                    (momentum_after_initial_kick / particle.rest_mass) * dt;
    if (!finite_momentum(initial_impulse) || !finite_momentum(momentum_after_initial_kick) ||
        !finite_position(position)) {
        return std::unexpected(NewtonianStepError::NumericalOverflow);
    }

    const spacetime::Event<2> final_event{time + dt, position, spacetime::FrameId{1}};
    const auto final_gravity = gravity.sample(final_event);
    if (!finite_gravity(final_gravity)) {
        return std::unexpected(NewtonianStepError::NonFiniteFieldSample);
    }
    const math::Vector<2, units::Momentum> final_impulse = final_gravity * particle.rest_mass * half_dt;
    const math::Vector<2, units::Momentum> unconstrained_momentum =
        momentum_after_initial_kick + final_impulse;
    if (!finite_momentum(final_impulse) || !finite_momentum(unconstrained_momentum)) {
        return std::unexpected(NewtonianStepError::NumericalOverflow);
    }

    auto momentum = unconstrained_momentum;
    auto constraint_reaction = zero_momentum_vector();
    if (particle.constraint == world::KinematicConstraint::Fixed) {
        constraint_reaction = zero_momentum_vector() - unconstrained_momentum;
        momentum = zero_momentum_vector();
    }
    return KdkParticleResult{
        position,
        momentum,
        initial_gravity,
        final_gravity,
        initial_impulse,
        final_impulse,
        momentum_after_initial_kick,
        zero_momentum_vector(),
        constraint_reaction,
        zero_momentum_vector(),
        units::kilogram_square_metres_per_second(0.0),
    };
}

struct KdkSystemResult {
    std::vector<KdkParticleResult> particles;
    std::vector<FrictionlessContactSystemEvent2> contacts;
    units::Energy unresolved_internal_energy_flux{units::joules(0.0)};
};

[[nodiscard]] std::expected<KdkSystemResult, NewtonianStepError> integrate_contact_system_kdk(
    const world::ParticleSnapshot2& snapshot,
    const fields::EffectiveGravityField<2>& gravity,
    const boundaries::BoundaryRegistry& boundaries,
    const NewtonianContactEnvironment2& contact,
    spacetime::SimulationTime time,
    units::Duration dt)
{
    const auto half_dt = dt / 2.0;
    KdkSystemResult system;
    system.particles.resize(snapshot.particles.size());
    std::vector<ContactBody2> contact_bodies;
    contact_bodies.reserve(contact.disc_colliders.size());
    std::map<world::ParticleId, std::size_t> particle_indices;

    for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
        const auto& particle = snapshot.particles[index];
        particle_indices.emplace(particle.id, index);
        const auto initial_gravity = gravity.sample(
            spacetime::Event<2>{time, particle.position, spacetime::FrameId{1}});
        if (!finite_gravity(initial_gravity)) {
            return std::unexpected(NewtonianStepError::NonFiniteFieldSample);
        }
        const auto initial_impulse = initial_gravity * particle.rest_mass * half_dt;
        const auto momentum_after_initial_kick = particle.momentum + initial_impulse;
        if (!finite_momentum(initial_impulse) || !finite_momentum(momentum_after_initial_kick)) {
            return std::unexpected(NewtonianStepError::NumericalOverflow);
        }

        auto& result = system.particles[index];
        result.position = particle.position;
        result.momentum = momentum_after_initial_kick;
        result.initial_gravity = initial_gravity;
        result.initial_gravity_impulse = initial_impulse;
        result.momentum_after_contact_drift = momentum_after_initial_kick;
        result.internal_contact_impulse = zero_momentum_vector();
        result.constraint_reaction_impulse = zero_momentum_vector();
        result.boundary_impulse = zero_momentum_vector();
        result.boundary_angular_impulse = units::kilogram_square_metres_per_second(0.0);

        const auto* collider = contact.disc_colliders.find(particle.id);
        if (collider != nullptr) {
            auto contact_state = particle;
            contact_state.momentum = momentum_after_initial_kick;
            contact_bodies.push_back(ContactBody2{std::move(contact_state), collider->radius});
        } else if (particle.constraint == world::KinematicConstraint::Free) {
            result.position = particle.position +
                              (momentum_after_initial_kick / particle.rest_mass) * dt;
        }
        if (!finite_position(result.position)) {
            return std::unexpected(NewtonianStepError::NumericalOverflow);
        }
    }

    if (!contact_bodies.empty()) {
        const auto expected_contact_body_count = contact_bodies.size();
        auto drift = resolve_frictionless_contact_system_drift(
            std::move(contact_bodies),
            boundaries,
            contact.materials,
            contact.mechanical_responses,
            dt,
            contact.configuration);
        if (!drift) {
            return std::unexpected(map_contact_error(drift.error()));
        }
        if (drift->bodies.size() != expected_contact_body_count ||
            drift->particle_impulses.size() != expected_contact_body_count) {
            return std::unexpected(NewtonianStepError::ContactResolutionFailed);
        }
        std::size_t mapped_contact_body_count = 0;
        for (std::size_t contact_index = 0; contact_index < drift->bodies.size(); ++contact_index) {
            const auto particle_id = drift->bodies[contact_index].state.id;
            const auto found = particle_indices.find(particle_id);
            if (found == particle_indices.end() ||
                drift->particle_impulses[contact_index].particle != particle_id) {
                return std::unexpected(NewtonianStepError::ContactResolutionFailed);
            }
            auto& result = system.particles[found->second];
            result.position = drift->bodies[contact_index].state.position;
            result.momentum_after_contact_drift = drift->bodies[contact_index].state.momentum;
            result.internal_contact_impulse = drift->particle_impulses[contact_index].internal_impulse;
            result.boundary_impulse = drift->particle_impulses[contact_index].boundary_impulse;
            ++mapped_contact_body_count;
        }
        auto reconstructed_boundary_angular_impulse =
            units::kilogram_square_metres_per_second(0.0);
        for (const auto& event : drift->contacts) {
            if (event.kind != FrictionlessContactSystemEventKind::StaticBoundary) {
                continue;
            }
            const auto found = particle_indices.find(event.first_particle);
            if (found == particle_indices.end()) {
                return std::unexpected(NewtonianStepError::ContactResolutionFailed);
            }
            const auto event_angular_impulse =
                angular_momentum_about_origin(event.contact_point, event.impulse_on_first);
            system.particles[found->second].boundary_angular_impulse += event_angular_impulse;
            reconstructed_boundary_angular_impulse += event_angular_impulse;
        }
        if (mapped_contact_body_count != expected_contact_body_count ||
            reconstructed_boundary_angular_impulse != drift->boundary_angular_impulse) {
            return std::unexpected(NewtonianStepError::ContactResolutionFailed);
        }
        system.contacts = std::move(drift->contacts);
        system.unresolved_internal_energy_flux = drift->unresolved_internal_energy_flux;
    }

    if (!std::isfinite(units::in_joules(system.unresolved_internal_energy_flux)) ||
        units::in_joules(system.unresolved_internal_energy_flux) < 0.0) {
        return std::unexpected(NewtonianStepError::NumericalOverflow);
    }
    for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
        const auto& particle = snapshot.particles[index];
        auto& result = system.particles[index];
        const auto final_gravity = gravity.sample(
            spacetime::Event<2>{time + dt, result.position, spacetime::FrameId{1}});
        if (!finite_gravity(final_gravity)) {
            return std::unexpected(NewtonianStepError::NonFiniteFieldSample);
        }
        const auto final_impulse = final_gravity * particle.rest_mass * half_dt;
        const auto unconstrained_momentum = result.momentum_after_contact_drift + final_impulse;
        if (!finite_momentum(result.momentum_after_contact_drift) ||
            !finite_momentum(result.internal_contact_impulse) ||
            !finite_momentum(result.boundary_impulse) || !finite_momentum(final_impulse) ||
            !finite_momentum(unconstrained_momentum) || !finite_position(result.position) ||
            !std::isfinite(units::in_kilogram_square_metres_per_second(
                result.boundary_angular_impulse))) {
            return std::unexpected(NewtonianStepError::NumericalOverflow);
        }
        result.final_gravity = final_gravity;
        result.final_gravity_impulse = final_impulse;
        result.momentum = unconstrained_momentum;
        if (particle.constraint == world::KinematicConstraint::Fixed) {
            result.constraint_reaction_impulse = zero_momentum_vector() - unconstrained_momentum;
            result.momentum = zero_momentum_vector();
            result.position = particle.position;
        }
    }
    return system;
}

[[nodiscard]] double normalized_step_difference(
    const KdkParticleResult& full_step,
    const KdkParticleResult& two_half_steps)
{
    const auto position_difference = full_step.position - two_half_steps.position;
    const auto momentum_difference = full_step.momentum - two_half_steps.momentum;
    const auto position_error = std::hypot(
        units::in_metres(position_difference[0]),
        units::in_metres(position_difference[1]));
    const auto momentum_error = std::hypot(
        units::in_kilogram_metres_per_second(momentum_difference[0]),
        units::in_kilogram_metres_per_second(momentum_difference[1]));
    const auto position_magnitude = [](const spacetime::WorldPosition<2>& value) {
        return std::hypot(units::in_metres(value[0]), units::in_metres(value[1]));
    };
    const auto momentum_magnitude = [](const math::Vector<2, units::Momentum>& value) {
        return std::hypot(
            units::in_kilogram_metres_per_second(value[0]),
            units::in_kilogram_metres_per_second(value[1]));
    };
    constexpr double relative_tolerance = 1.0e-9;
    constexpr double absolute_position_tolerance_metres = 1.0e-12;
    constexpr double absolute_momentum_tolerance = 1.0e-12;
    const auto position_scale = std::max(
        position_magnitude(full_step.position),
        position_magnitude(two_half_steps.position));
    const auto momentum_scale = std::max(
        momentum_magnitude(full_step.momentum),
        momentum_magnitude(two_half_steps.momentum));
    return std::max(
        position_error / (absolute_position_tolerance_metres + relative_tolerance * position_scale),
        momentum_error / (absolute_momentum_tolerance + relative_tolerance * momentum_scale));
}

[[nodiscard]] conservation::ParticleBalanceReport2 audit_balance(
    const world::ParticleSnapshot2& snapshot,
    const std::vector<ParticleUpdate2>& updates,
    units::Energy unresolved_internal_energy_flux)
{
    auto mass_before = units::kilograms(0.0);
    auto mass_after = units::kilograms(0.0);
    auto momentum_before = zero_momentum_vector();
    auto momentum_after = zero_momentum_vector();
    auto momentum_source = zero_momentum_vector();
    auto angular_before = units::kilogram_square_metres_per_second(0.0);
    auto angular_after = units::kilogram_square_metres_per_second(0.0);
    auto angular_source = units::kilogram_square_metres_per_second(0.0);
    auto kinetic_before = units::joules(0.0);
    auto kinetic_after = units::joules(0.0);
    auto kinetic_source = units::joules(0.0);
    std::vector<conservation::ParticleBalanceEntry2> entries;
    entries.reserve(snapshot.particles.size());

    for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
        const auto& particle = snapshot.particles[index];
        const auto& update = updates[index];
        const auto momentum_after_initial_gravity = particle.momentum + update.initial_gravity_impulse;
        const auto initial_gravity_work = impulse_work(
            particle.momentum,
            momentum_after_initial_gravity,
            update.initial_gravity_impulse,
            particle.rest_mass);
        const auto momentum_after_contact = update.momentum_after_contact_drift;
        const auto momentum_after_final_gravity = momentum_after_contact + update.final_gravity_impulse;
        const auto final_gravity_work = impulse_work(
            momentum_after_contact,
            momentum_after_final_gravity,
            update.final_gravity_impulse,
            particle.rest_mass);
        const auto gravity_work = initial_gravity_work + final_gravity_work;
        const auto momentum_after_constraint = momentum_after_final_gravity + update.constraint_reaction_impulse;
        const auto constraint_work = impulse_work(
            momentum_after_final_gravity,
            momentum_after_constraint,
            update.constraint_reaction_impulse,
            particle.rest_mass);
        const auto boundary_work = units::joules(0.0);
        const auto total_impulse =
            update.gravity_impulse + update.constraint_reaction_impulse + update.boundary_impulse;

        conservation::ParticleBalanceEntry2 entry{};
        entry.particle_id = particle.id.value();
        entry.mass = particle.rest_mass;
        entry.momentum_before = particle.momentum;
        entry.momentum_after = update.momentum;
        entry.gravity_impulse = update.gravity_impulse;
        entry.internal_contact_impulse = update.internal_contact_impulse;
        entry.constraint_reaction_impulse = update.constraint_reaction_impulse;
        entry.boundary_impulse = update.boundary_impulse;
        entry.angular_momentum_before = angular_momentum_about_origin(particle.position, particle.momentum);
        entry.angular_momentum_after = angular_momentum_about_origin(update.position, update.momentum);
        entry.external_angular_impulse =
            angular_momentum_about_origin(particle.position, update.initial_gravity_impulse) +
            update.boundary_angular_impulse +
            angular_momentum_about_origin(
                update.position,
                update.final_gravity_impulse + update.constraint_reaction_impulse);
        entry.kinetic_energy_before = kinetic_energy(particle.momentum, particle.rest_mass);
        entry.kinetic_energy_after = kinetic_energy(update.momentum, particle.rest_mass);
        entry.gravity_work = gravity_work;
        entry.constraint_work = constraint_work;
        entry.boundary_work = boundary_work;

        mass_before += particle.rest_mass;
        mass_after += particle.rest_mass;
        momentum_before = momentum_before + particle.momentum;
        momentum_after = momentum_after + update.momentum;
        momentum_source = momentum_source + total_impulse;
        angular_before += entry.angular_momentum_before;
        angular_after += entry.angular_momentum_after;
        angular_source += entry.external_angular_impulse;
        kinetic_before += entry.kinetic_energy_before;
        kinetic_after += entry.kinetic_energy_after;
        kinetic_source += gravity_work + constraint_work;
        entries.push_back(std::move(entry));
    }

    const auto zero_angular = units::kilogram_square_metres_per_second(0.0);
    const auto zero_energy = units::joules(0.0);
    conservation::ParticleBalanceReport2 report{
        std::move(entries),
        {mass_before, mass_after, units::kilograms(0.0), units::kilograms(0.0)},
        {momentum_before, momentum_after, momentum_source, zero_momentum_vector()},
        {angular_before, angular_after, angular_source, zero_angular},
        {kinetic_before, kinetic_after, kinetic_source, unresolved_internal_energy_flux},
        conservation::AuditStatus::Closed,
        any_exchange(momentum_source) ? conservation::AuditStatus::BalancedWithSources
                                      : conservation::AuditStatus::Closed,
        units::in_kilogram_square_metres_per_second(angular_source) != 0.0
            ? conservation::AuditStatus::BalancedWithSources
            : conservation::AuditStatus::Closed,
        units::in_joules(kinetic_source) != 0.0 ||
                units::in_joules(unresolved_internal_energy_flux) != 0.0
            ? conservation::AuditStatus::BalancedWithSources
            : conservation::AuditStatus::Closed,
        conservation::AuditStatus::MissingRepresentation,
    };
    if (!report.within()) {
        report.mass_status = report.normalized_mass_residual() <= 1.0 ? report.mass_status
                                                                      : conservation::AuditStatus::Failed;
        report.linear_momentum_status = report.normalized_momentum_residual() <= 1.0
                                            ? report.linear_momentum_status
                                            : conservation::AuditStatus::Failed;
        report.angular_momentum_status = report.normalized_angular_momentum_residual() <= 1.0
                                             ? report.angular_momentum_status
                                             : conservation::AuditStatus::Failed;
        report.kinetic_energy_status = report.normalized_kinetic_energy_residual() <= 1.0
                                           ? report.kinetic_energy_status
                                           : conservation::AuditStatus::Failed;
    }
    return report;
}

[[nodiscard]] diagnostics::ConservationAuditStatus diagnostic_status(conservation::AuditStatus status)
{
    switch (status) {
    case conservation::AuditStatus::Closed:
        return diagnostics::ConservationAuditStatus::Closed;
    case conservation::AuditStatus::BalancedWithSources:
        return diagnostics::ConservationAuditStatus::BalancedWithSources;
    case conservation::AuditStatus::Failed:
        return diagnostics::ConservationAuditStatus::Failed;
    case conservation::AuditStatus::NotClaimed:
        return diagnostics::ConservationAuditStatus::NotClaimed;
    case conservation::AuditStatus::MissingRepresentation:
        return diagnostics::ConservationAuditStatus::MissingRepresentation;
    case conservation::AuditStatus::Deferred:
        return diagnostics::ConservationAuditStatus::DeferredUntilFoundationValidation;
    }
    return diagnostics::ConservationAuditStatus::Failed;
}

[[nodiscard]] diagnostics::ConstraintReport constraint_report(
    const world::ParticleSnapshot2& snapshot,
    const std::vector<ParticleUpdate2>& updates)
{
    diagnostics::ConstraintReport report;
    for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
        const auto& particle = snapshot.particles[index];
        if (particle.constraint != world::KinematicConstraint::Fixed) {
            continue;
        }
        const auto& update = updates[index];
        const auto displacement = update.position - particle.position;
        const auto velocity = update.momentum / particle.rest_mass;
        const auto position_residual = std::hypot(
            units::in_metres(displacement[0]),
            units::in_metres(displacement[1]));
        const auto velocity_residual = std::hypot(
            units::in_metres_per_second(velocity[0]),
            units::in_metres_per_second(velocity[1]));
        const auto normalized = std::max(position_residual / 1.0e-12, velocity_residual / 1.0e-12);
        report.maximum_residual = std::max(report.maximum_residual, normalized);
        report.projected = report.projected || any_exchange(update.constraint_reaction_impulse);
        report.records.push_back(diagnostics::ConstraintRecord2{
            particle.id.value(),
            units::metres(position_residual),
            units::metres_per_second(velocity_residual),
            update.constraint_reaction_impulse,
            any_exchange(update.constraint_reaction_impulse),
        });
    }
    report.satisfied = std::isfinite(report.maximum_residual) && report.maximum_residual <= 1.0;
    return report;
}

[[nodiscard]] bool physical_proposal_equal(
    const NewtonianStepProposal2& submitted,
    const NewtonianStepProposal2& canonical)
{
    return submitted.source == canonical.source && submitted.dt == canonical.dt &&
           submitted.updates == canonical.updates && submitted.contacts == canonical.contacts &&
           submitted.unresolved_internal_energy_flux == canonical.unresolved_internal_energy_flux &&
           submitted.conservation == canonical.conservation;
}

[[nodiscard]] NewtonianStepError map_mutation_error(world::ParticleMutationError error)
{
    switch (error) {
    case world::ParticleMutationError::StaleRevision:
        return NewtonianStepError::StaleSnapshot;
    case world::ParticleMutationError::MissingParticle:
        return NewtonianStepError::MissingParticle;
    case world::ParticleMutationError::RevisionExhausted:
        return NewtonianStepError::SourceRevisionExhausted;
    case world::ParticleMutationError::NonFiniteState:
        return NewtonianStepError::NumericalOverflow;
    case world::ParticleMutationError::InvalidConstraint:
    case world::ParticleMutationError::FixedParticleHasMomentum:
        return NewtonianStepError::InvalidConstraint;
    case world::ParticleMutationError::InvalidIdentifier:
    case world::ParticleMutationError::DuplicateIdentifier:
    case world::ParticleMutationError::NonPositiveMass:
    case world::ParticleMutationError::ConstrainedParticle:
    case world::ParticleMutationError::IncompleteUpdate:
        return NewtonianStepError::CommitRejected;
    }
    return NewtonianStepError::CommitRejected;
}

}  // namespace

scheduler::SolverDescriptor NewtonianParticleSolver2::descriptor() const
{
    return scheduler::SolverDescriptor{
        scheduler::SolverId{1},
        "NewtonianParticleDynamics",
        state::AccessDescriptor{
            state::ChannelSet{
                state::standard_channels::position_id,
                state::standard_channels::momentum_id,
                state::standard_channels::rest_mass_id,
                state::standard_channels::effective_gravity_id,
            },
            state::ChannelSet{
                state::standard_channels::position_id,
                state::standard_channels::momentum_id,
            },
        },
        scheduler::SolverExecutionContract{
            ontology::theory_ids::newtonian_particle_dynamics,
            scheduler::ValidityContractRevision{1},
            scheduler::StepContractRevision{newtonian_step_contract_revision},
            scheduler::IntegratorContractRevision{newtonian_integrator_revision},
            scheduler::StepControlPolicy::Fixed,
            scheduler::IntegratorFamily::OperatorSplit,
            {
                ontology::conserved_quantities::mass,
                ontology::conserved_quantities::linear_momentum,
                ontology::conserved_quantities::angular_momentum,
            },
            {{
                ontology::constraints::fixed_kinematic,
                scheduler::ConstraintCapabilityKind::Evaluates |
                    scheduler::ConstraintCapabilityKind::Projects,
            }},
            {{
                ontology::boundary_requirements::particle_domain,
                scheduler::BoundaryCapabilityKind::Samples |
                    scheduler::BoundaryCapabilityKind::Enforces |
                    scheduler::BoundaryCapabilityKind::AccountsFlux,
            }},
            scheduler::DeterministicNumericPolicy{
                scheduler::DeterminismGuarantee::BitwiseWithinBuild,
                scheduler::FloatingPointPolicy::StrictIeee754,
                scheduler::NonFinitePolicy::RejectProposal,
                scheduler::IterationOrderPolicy::CanonicalPersistentId,
                scheduler::ParallelAccumulationPolicy::SerialCanonical,
            },
        },
    };
}

std::expected<NewtonianStepProposal2, NewtonianStepError> NewtonianParticleSolver2::propose(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    units::Duration dt) const
{
    return propose_impl(world, gravity, nullptr, dt);
}

std::expected<NewtonianStepProposal2, NewtonianStepError> NewtonianParticleSolver2::propose(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2& contact,
    units::Duration dt) const
{
    return propose_impl(world, gravity, &contact, dt);
}

std::expected<NewtonianStepProposal2, NewtonianStepError> NewtonianParticleSolver2::propose_impl(
    const world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2* contact,
    units::Duration dt) const
{
    if (!valid_validity_configuration(validity_.configuration())) {
        return std::unexpected(NewtonianStepError::InvalidValidityConfiguration);
    }
    const auto dt_seconds = units::in_seconds(dt);
    if (!std::isfinite(dt_seconds)) {
        return std::unexpected(NewtonianStepError::NonFiniteTimeStep);
    }
    if (dt_seconds <= 0.0) {
        return std::unexpected(NewtonianStepError::NonPositiveTimeStep);
    }
    const auto time_seconds = units::in_seconds(world.simulation_time.elapsed());
    if (!std::isfinite(time_seconds) || time_seconds < 0.0) {
        return std::unexpected(NewtonianStepError::NonFiniteSimulationTime);
    }
    if (!std::isfinite(time_seconds + dt_seconds)) {
        return std::unexpected(NewtonianStepError::NumericalOverflow);
    }
    if (world.tick == std::numeric_limits<std::uint64_t>::max() ||
        world.particles.revision() == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(NewtonianStepError::SourceRevisionExhausted);
    }
    if (contact != nullptr) {
        const auto contact_validity = validate_contact_environment(world, *contact);
        if (!contact_validity) {
            return std::unexpected(contact_validity.error());
        }
    }

    const auto wall_start = std::chrono::steady_clock::now();
    const auto snapshot = world.particles.snapshot();
    const auto initial_validity = validity_.evaluate(snapshot);
    if (!initial_validity.valid) {
        return std::unexpected(
            initial_validity.state == diagnostics::ValidityState::OutsideDomainNoFallback
                ? NewtonianStepError::OutsideValidityDomain
                : NewtonianStepError::NonFiniteParticleState);
    }

    NewtonianStepProposal2 proposal;
    proposal.source = source_stamp(world, gravity, contact, validity_.configuration());
    proposal.dt = dt;
    proposal.updates.reserve(snapshot.particles.size());
    std::set<world::ParticleId> particle_ids;
    double maximum_integration_error = 0.0;
    if (snapshot.particles.size() > std::numeric_limits<std::size_t>::max() / 6U) {
        return std::unexpected(NewtonianStepError::NumericalOverflow);
    }

    for (const auto& particle : snapshot.particles) {
        if (!particle.id || !particle.material || !particle_ids.insert(particle.id).second ||
            !finite_position(particle.position) || !finite_momentum(particle.momentum) ||
            !std::isfinite(units::in_kilograms(particle.rest_mass))) {
            return std::unexpected(NewtonianStepError::NonFiniteParticleState);
        }
        if (units::in_kilograms(particle.rest_mass) <= 0.0) {
            return std::unexpected(NewtonianStepError::NonPositiveMass);
        }
        if (particle.constraint != world::KinematicConstraint::Free &&
            particle.constraint != world::KinematicConstraint::Fixed) {
            return std::unexpected(NewtonianStepError::InvalidConstraint);
        }
        if (particle.constraint == world::KinematicConstraint::Fixed && !zero_momentum(particle.momentum)) {
            return std::unexpected(NewtonianStepError::InvalidConstraint);
        }
    }

    const auto append_update = [&](const world::ParticleState2& particle,
                                   const KdkParticleResult& integrated,
                                   const KdkParticleResult& two_half_steps)
        -> std::expected<void, NewtonianStepError> {
        const auto integration_error = normalized_step_difference(integrated, two_half_steps);
        if (!std::isfinite(integration_error) || integration_error < 0.0) {
            return std::unexpected(NewtonianStepError::NumericalOverflow);
        }
        maximum_integration_error = std::max(maximum_integration_error, integration_error);

        ParticleUpdate2 update{
            particle.id,
            integrated.position,
            integrated.momentum,
            integrated.initial_gravity,
            integrated.final_gravity,
            integrated.initial_gravity_impulse,
            integrated.final_gravity_impulse,
            integrated.initial_gravity_impulse + integrated.final_gravity_impulse,
            integrated.momentum_after_contact_drift,
            integrated.internal_contact_impulse,
            integrated.constraint_reaction_impulse,
            integrated.boundary_impulse,
            integrated.boundary_angular_impulse,
        };
        proposal.updates.push_back(std::move(update));
        return {};
    };

    const auto half_dt = dt / 2.0;
    if (contact != nullptr) {
        const auto integrated = integrate_contact_system_kdk(
            snapshot,
            gravity,
            world.boundaries,
            *contact,
            world.simulation_time,
            dt);
        if (!integrated) {
            return std::unexpected(integrated.error());
        }
        const auto first_half = integrate_contact_system_kdk(
            snapshot,
            gravity,
            world.boundaries,
            *contact,
            world.simulation_time,
            half_dt);
        if (!first_half) {
            return std::unexpected(first_half.error());
        }
        if (integrated->particles.size() != snapshot.particles.size() ||
            first_half->particles.size() != snapshot.particles.size()) {
            return std::unexpected(NewtonianStepError::ContactResolutionFailed);
        }

        auto midpoint_snapshot = snapshot;
        for (std::size_t index = 0; index < midpoint_snapshot.particles.size(); ++index) {
            midpoint_snapshot.particles[index].position = first_half->particles[index].position;
            midpoint_snapshot.particles[index].momentum = first_half->particles[index].momentum;
        }
        const auto second_half = integrate_contact_system_kdk(
            midpoint_snapshot,
            gravity,
            world.boundaries,
            *contact,
            world.simulation_time + half_dt,
            half_dt);
        if (!second_half) {
            return std::unexpected(second_half.error());
        }
        if (second_half->particles.size() != snapshot.particles.size()) {
            return std::unexpected(NewtonianStepError::ContactResolutionFailed);
        }

        proposal.contacts = integrated->contacts;
        proposal.unresolved_internal_energy_flux = integrated->unresolved_internal_energy_flux;
        for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
            const auto appended = append_update(
                snapshot.particles[index],
                integrated->particles[index],
                second_half->particles[index]);
            if (!appended) {
                return std::unexpected(appended.error());
            }
        }
    } else {
        for (const auto& particle : snapshot.particles) {
            const auto integrated = integrate_kick_drift_kick(
                particle,
                gravity,
                world.simulation_time,
                dt);
            if (!integrated) {
                return std::unexpected(integrated.error());
            }
            const auto first_half = integrate_kick_drift_kick(
                particle,
                gravity,
                world.simulation_time,
                half_dt);
            if (!first_half) {
                return std::unexpected(first_half.error());
            }
            auto midpoint_particle = particle;
            midpoint_particle.position = first_half->position;
            midpoint_particle.momentum = first_half->momentum;
            const auto second_half = integrate_kick_drift_kick(
                midpoint_particle,
                gravity,
                world.simulation_time + half_dt,
                half_dt);
            if (!second_half) {
                return std::unexpected(second_half.error());
            }
            const auto appended = append_update(particle, *integrated, *second_half);
            if (!appended) {
                return std::unexpected(appended.error());
            }
        }
    }

    if (source_stamp(world, gravity, contact, validity_.configuration()) != proposal.source) {
        return std::unexpected(NewtonianStepError::StaleSnapshot);
    }

    auto proposed_snapshot = snapshot;
    for (std::size_t index = 0; index < proposed_snapshot.particles.size(); ++index) {
        proposed_snapshot.particles[index].position = proposal.updates[index].position;
        proposed_snapshot.particles[index].momentum = proposal.updates[index].momentum;
    }
    const auto proposed_validity = validity_.evaluate(proposed_snapshot);
    if (!proposed_validity.valid) {
        return std::unexpected(
            proposed_validity.state == diagnostics::ValidityState::OutsideDomainNoFallback
                ? NewtonianStepError::OutsideValidityDomain
                : NewtonianStepError::NumericalOverflow);
    }

    proposal.conservation = audit_balance(
        snapshot,
        proposal.updates,
        proposal.unresolved_internal_energy_flux);
    if (!proposal.conservation.within()) {
        return std::unexpected(NewtonianStepError::ConservationAuditFailed);
    }
    const auto constraints = constraint_report(snapshot, proposal.updates);
    if (!constraints.satisfied) {
        return std::unexpected(NewtonianStepError::ConstraintAuditFailed);
    }
    const auto wall_end = std::chrono::steady_clock::now();
    const auto& reported_validity =
        proposed_validity.observed >= initial_validity.observed ? proposed_validity : initial_validity;
    proposal.diagnostic = diagnostics::SolverStepDiagnostic{
        "NewtonianParticleDynamics",
        world.tick,
        world.simulation_time,
        dt,
        snapshot.particles.size(),
        snapshot.particles.size() * 6U,
        1,
        diagnostics::ErrorEstimate{
            diagnostics::EstimateStatus::Estimated,
            maximum_integration_error,
            0.0,
            0.0,
            0.0,
        },
        reported_validity,
        constraints,
        diagnostics::ConservationSummary{},
        std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start),
    };
    proposal.diagnostic.conservation.mass = diagnostic_status(proposal.conservation.mass_status);
    proposal.diagnostic.conservation.momentum = diagnostic_status(proposal.conservation.linear_momentum_status);
    proposal.diagnostic.conservation.angular_momentum =
        diagnostic_status(proposal.conservation.angular_momentum_status);
    proposal.diagnostic.conservation.kinetic_energy =
        diagnostic_status(proposal.conservation.kinetic_energy_status);
    proposal.diagnostic.conservation.energy =
        diagnostic_status(proposal.conservation.total_mechanical_energy_status);
    proposal.diagnostic.error.conservation_residual = proposal.conservation.maximum_normalized_residual();
    proposal.diagnostic.error.model_error = proposal.diagnostic.validity.estimated_model_error;
    return proposal;
}

std::expected<void, NewtonianStepError> NewtonianParticleSolver2::commit(
    world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianStepProposal2& proposal) const
{
    return commit_impl(world, gravity, nullptr, proposal);
}

std::expected<void, NewtonianStepError> NewtonianParticleSolver2::commit(
    world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2& contact,
    const NewtonianStepProposal2& proposal) const
{
    return commit_impl(world, gravity, &contact, proposal);
}

std::expected<void, NewtonianStepError> NewtonianParticleSolver2::commit_impl(
    world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2* contact,
    const NewtonianStepProposal2& proposal) const
{
    if (!valid_validity_configuration(validity_.configuration())) {
        return std::unexpected(NewtonianStepError::InvalidValidityConfiguration);
    }
    if (contact != nullptr) {
        const auto contact_validity = validate_contact_environment(world, *contact);
        if (!contact_validity) {
            return std::unexpected(contact_validity.error());
        }
    }
    if (source_stamp(world, gravity, contact, validity_.configuration()) != proposal.source) {
        return std::unexpected(NewtonianStepError::StaleSnapshot);
    }
    const auto canonical = propose_impl(world, gravity, contact, proposal.dt);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    if (!physical_proposal_equal(proposal, *canonical)) {
        return std::unexpected(NewtonianStepError::InvalidProposal);
    }

    std::vector<world::ParticleKinematicUpdate2> updates;
    updates.reserve(proposal.updates.size());
    for (const auto& update : proposal.updates) {
        updates.push_back(world::ParticleKinematicUpdate2{update.id, update.position, update.momentum});
    }
    const auto committed = world.particles.apply_kinematic_updates(proposal.source.particle_revision, updates);
    if (!committed) {
        return std::unexpected(map_mutation_error(committed.error()));
    }
    world.simulation_time = proposal.source.simulation_time + proposal.dt;
    world.tick = proposal.source.tick + 1;
    return {};
}

std::expected<NewtonianStepProposal2, NewtonianStepError> NewtonianParticleSolver2::advance(
    world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    units::Duration dt) const
{
    auto proposal = propose(world, gravity, dt);
    if (!proposal) {
        return std::unexpected(proposal.error());
    }
    const auto committed = commit(world, gravity, *proposal);
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return proposal;
}

std::expected<NewtonianStepProposal2, NewtonianStepError> NewtonianParticleSolver2::advance(
    world::WorldState2& world,
    const fields::EffectiveGravityField<2>& gravity,
    const NewtonianContactEnvironment2& contact,
    units::Duration dt) const
{
    auto proposal = propose_impl(world, gravity, &contact, dt);
    if (!proposal) {
        return std::unexpected(proposal.error());
    }
    const auto committed = commit_impl(world, gravity, &contact, *proposal);
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return proposal;
}

diagnostics::ValidityReport NewtonianValidityEvaluator2::evaluate(const world::ParticleSnapshot2& snapshot) const
{
    const auto invariant_speed = units::in_metres_per_second(configuration_.invariant_speed);
    const auto beta_limit = configuration_.maximum_beta;
    if (!std::isfinite(invariant_speed) || invariant_speed <= 0.0 || !std::isfinite(beta_limit) ||
        beta_limit <= 0.0) {
        return diagnostics::ValidityReport{
            .valid = false,
            .theory = "NewtonianParticleDynamics",
            .criterion = "beta",
            .state = diagnostics::ValidityState::UnsupportedState,
            .observed = 0.0,
            .limit = beta_limit,
            .worst_particle_id = std::nullopt,
            .estimated_model_error = std::numeric_limits<double>::infinity(),
            .dominant_violation = diagnostics::ValidityReasonCode::UnsupportedState,
        };
    }

    double maximum_beta = 0.0;
    std::optional<std::uint64_t> worst_particle;
    for (const auto& particle : snapshot.particles) {
        const auto rest_mass = units::in_kilograms(particle.rest_mass);
        if (!particle.id || !particle.material || !std::isfinite(rest_mass) || rest_mass <= 0.0 ||
            !finite_position(particle.position) || !finite_momentum(particle.momentum) ||
            (particle.constraint != world::KinematicConstraint::Free &&
             particle.constraint != world::KinematicConstraint::Fixed) ||
            (particle.constraint == world::KinematicConstraint::Fixed && !zero_momentum(particle.momentum))) {
            return diagnostics::ValidityReport{
                .valid = false,
                .theory = "NewtonianParticleDynamics",
                .criterion = "beta",
                .state = diagnostics::ValidityState::UnsupportedState,
                .observed = maximum_beta,
                .limit = beta_limit,
                .worst_particle_id = worst_particle,
                .estimated_model_error = std::numeric_limits<double>::infinity(),
                .dominant_violation = diagnostics::ValidityReasonCode::UnsupportedState,
            };
        }
        const auto velocity = world::velocity_of(particle);
        const auto beta = std::hypot(
                              units::in_metres_per_second(velocity[0]),
                              units::in_metres_per_second(velocity[1])) /
                          invariant_speed;
        if (!std::isfinite(beta)) {
            return diagnostics::ValidityReport{
                .valid = false,
                .theory = "NewtonianParticleDynamics",
                .criterion = "beta",
                .state = diagnostics::ValidityState::UnsupportedState,
                .observed = beta,
                .limit = beta_limit,
                .worst_particle_id = particle.id.value(),
                .estimated_model_error = std::numeric_limits<double>::infinity(),
                .dominant_violation = diagnostics::ValidityReasonCode::UnsupportedState,
            };
        }
        if (!worst_particle || beta > maximum_beta ||
            (beta == maximum_beta && particle.id.value() < *worst_particle)) {
            maximum_beta = beta;
            worst_particle = particle.id.value();
        }
    }

    const auto valid = maximum_beta <= beta_limit;
    const auto near_limit = valid && maximum_beta >= beta_limit * 0.8;
    return diagnostics::ValidityReport{
        .valid = valid,
        .theory = "NewtonianParticleDynamics",
        .criterion = "beta",
        .state = valid ? (near_limit ? diagnostics::ValidityState::NearLimit
                                    : diagnostics::ValidityState::WithinDomain)
                       : diagnostics::ValidityState::OutsideDomainNoFallback,
        .observed = maximum_beta,
        .limit = beta_limit,
        .worst_particle_id = worst_particle,
        .estimated_model_error = maximum_beta * maximum_beta,
        .dominant_violation = valid ? diagnostics::ValidityReasonCode::None
                                    : diagnostics::ValidityReasonCode::SpeedRatioExceeded,
    };
}

}  // namespace principia::solvers
