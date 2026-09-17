#pragma once

#include <principia/math/vector.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace principia::conservation {

enum class AuditStatus : std::uint8_t {
    Closed,
    BalancedWithSources,
    Failed,
    NotClaimed,
    MissingRepresentation,
    Deferred,
};

// Sign convention: sources are positive into the audited system and fluxes are
// positive outward. A closed balance satisfies after = before + source - flux.
template <typename Quantity>
struct Balance {
    Quantity before;
    Quantity after;
    Quantity source_into_system;
    Quantity outward_flux;

    [[nodiscard]] constexpr Quantity residual() const
    {
        return after - before - source_into_system + outward_flux;
    }

    friend bool operator==(const Balance&, const Balance&) = default;
};

using MomentumVector2 = math::Vector<2, units::Momentum>;

struct ParticleBalanceEntry2 {
    std::uint64_t particle_id{};
    units::Mass mass{units::kilograms(0.0)};
    MomentumVector2 momentum_before;
    MomentumVector2 momentum_after;
    MomentumVector2 gravity_impulse;
    // Internal pair impulses are retained per particle for provenance, but are
    // not counted as aggregate sources: their canonical system sum is zero.
    MomentumVector2 internal_contact_impulse;
    MomentumVector2 constraint_reaction_impulse;
    MomentumVector2 boundary_impulse;
    units::AngularMomentum angular_momentum_before{units::kilogram_square_metres_per_second(0.0)};
    units::AngularMomentum angular_momentum_after{units::kilogram_square_metres_per_second(0.0)};
    units::AngularMomentum external_angular_impulse{units::kilogram_square_metres_per_second(0.0)};
    units::Energy kinetic_energy_before{units::joules(0.0)};
    units::Energy kinetic_energy_after{units::joules(0.0)};
    units::Energy gravity_work{units::joules(0.0)};
    units::Energy constraint_work{units::joules(0.0)};
    // Static boundaries exchange momentum but perform no external mechanical
    // work. Restitution loss is a system-level outward flux because a pair
    // loss cannot be attributed canonically to either particle.
    units::Energy boundary_work{units::joules(0.0)};

    friend bool operator==(const ParticleBalanceEntry2&, const ParticleBalanceEntry2&) = default;
};

struct ParticleBalanceTolerance2 {
    units::Mass absolute_mass{units::kilograms(1.0e-12)};
    units::Momentum absolute_linear_momentum{units::kilogram_metres_per_second(1.0e-12)};
    units::AngularMomentum absolute_angular_momentum{units::kilogram_square_metres_per_second(1.0e-12)};
    units::Energy absolute_energy{units::joules(1.0e-12)};
    double relative{1.0e-12};

    [[nodiscard]] bool valid() const noexcept
    {
        const auto mass = units::in_kilograms(absolute_mass);
        const auto momentum = units::in_kilogram_metres_per_second(absolute_linear_momentum);
        const auto angular = units::in_kilogram_square_metres_per_second(absolute_angular_momentum);
        const auto energy = units::in_joules(absolute_energy);
        return std::isfinite(mass) && mass >= 0.0 && std::isfinite(momentum) && momentum >= 0.0 &&
               std::isfinite(angular) && angular >= 0.0 && std::isfinite(energy) && energy >= 0.0 &&
               std::isfinite(relative) && relative >= 0.0;
    }
};

[[nodiscard]] constexpr bool closes_balance(AuditStatus status) noexcept
{
    return status == AuditStatus::Closed || status == AuditStatus::BalancedWithSources;
}

struct ParticleBalanceReport2 {
    std::vector<ParticleBalanceEntry2> entries;
    Balance<units::Mass> mass;
    Balance<MomentumVector2> linear_momentum;
    Balance<units::AngularMomentum> angular_momentum;
    Balance<units::Energy> kinetic_energy;
    AuditStatus mass_status{AuditStatus::Closed};
    AuditStatus linear_momentum_status{AuditStatus::Closed};
    AuditStatus angular_momentum_status{AuditStatus::Closed};
    AuditStatus kinetic_energy_status{AuditStatus::Closed};
    AuditStatus total_mechanical_energy_status{AuditStatus::MissingRepresentation};

    friend bool operator==(const ParticleBalanceReport2&, const ParticleBalanceReport2&) = default;

    [[nodiscard]] double normalized_mass_residual(const ParticleBalanceTolerance2& tolerance = {}) const
    {
        if (!tolerance.valid()) {
            return std::numeric_limits<double>::infinity();
        }
        const auto residual = std::abs(units::in_kilograms(mass.residual()));
        const auto scale = std::max({
            std::abs(units::in_kilograms(mass.before)),
            std::abs(units::in_kilograms(mass.after)),
            std::abs(units::in_kilograms(mass.source_into_system)),
            std::abs(units::in_kilograms(mass.outward_flux)),
        });
        const auto denominator = units::in_kilograms(tolerance.absolute_mass) + tolerance.relative * scale;
        if (!std::isfinite(residual) || !std::isfinite(scale) || !std::isfinite(denominator)) {
            return std::numeric_limits<double>::infinity();
        }
        return denominator > 0.0 ? residual / denominator
                                 : (residual == 0.0 ? 0.0 : std::numeric_limits<double>::infinity());
    }

    [[nodiscard]] double normalized_momentum_residual(const ParticleBalanceTolerance2& tolerance = {}) const
    {
        if (!tolerance.valid()) {
            return std::numeric_limits<double>::infinity();
        }
        const auto residual = linear_momentum.residual();
        const auto rx = units::in_kilogram_metres_per_second(residual[0]);
        const auto ry = units::in_kilogram_metres_per_second(residual[1]);
        const auto magnitude = [](const MomentumVector2& value) {
            return std::hypot(
                units::in_kilogram_metres_per_second(value[0]),
                units::in_kilogram_metres_per_second(value[1]));
        };
        const auto scale = std::max({
            magnitude(linear_momentum.before),
            magnitude(linear_momentum.after),
            magnitude(linear_momentum.source_into_system),
            magnitude(linear_momentum.outward_flux),
        });
        const auto denominator = units::in_kilogram_metres_per_second(tolerance.absolute_linear_momentum) +
                                 tolerance.relative * scale;
        const auto residual_magnitude = std::hypot(rx, ry);
        if (!std::isfinite(residual_magnitude) || !std::isfinite(scale) || !std::isfinite(denominator)) {
            return std::numeric_limits<double>::infinity();
        }
        return denominator > 0.0 ? residual_magnitude / denominator
                                 : (residual_magnitude == 0.0 ? 0.0 : std::numeric_limits<double>::infinity());
    }

    [[nodiscard]] double normalized_angular_momentum_residual(
        const ParticleBalanceTolerance2& tolerance = {}) const
    {
        if (!tolerance.valid()) {
            return std::numeric_limits<double>::infinity();
        }
        const auto scalar = [](units::AngularMomentum value) {
            return std::abs(units::in_kilogram_square_metres_per_second(value));
        };
        const auto residual = scalar(angular_momentum.residual());
        const auto scale = std::max({
            scalar(angular_momentum.before),
            scalar(angular_momentum.after),
            scalar(angular_momentum.source_into_system),
            scalar(angular_momentum.outward_flux),
        });
        const auto denominator = units::in_kilogram_square_metres_per_second(
                                     tolerance.absolute_angular_momentum) +
                                 tolerance.relative * scale;
        if (!std::isfinite(residual) || !std::isfinite(scale) || !std::isfinite(denominator)) {
            return std::numeric_limits<double>::infinity();
        }
        return denominator > 0.0 ? residual / denominator
                                 : (residual == 0.0 ? 0.0 : std::numeric_limits<double>::infinity());
    }

    [[nodiscard]] double normalized_kinetic_energy_residual(
        const ParticleBalanceTolerance2& tolerance = {}) const
    {
        if (!tolerance.valid()) {
            return std::numeric_limits<double>::infinity();
        }
        const auto scalar = [](units::Energy value) { return std::abs(units::in_joules(value)); };
        const auto residual = scalar(kinetic_energy.residual());
        const auto scale = std::max({
            scalar(kinetic_energy.before),
            scalar(kinetic_energy.after),
            scalar(kinetic_energy.source_into_system),
            scalar(kinetic_energy.outward_flux),
        });
        const auto denominator = units::in_joules(tolerance.absolute_energy) + tolerance.relative * scale;
        if (!std::isfinite(residual) || !std::isfinite(scale) || !std::isfinite(denominator)) {
            return std::numeric_limits<double>::infinity();
        }
        return denominator > 0.0 ? residual / denominator
                                 : (residual == 0.0 ? 0.0 : std::numeric_limits<double>::infinity());
    }

    [[nodiscard]] double maximum_normalized_residual(const ParticleBalanceTolerance2& tolerance = {}) const
    {
        const double residuals[]{
            normalized_mass_residual(tolerance),
            normalized_momentum_residual(tolerance),
            normalized_angular_momentum_residual(tolerance),
            normalized_kinetic_energy_residual(tolerance),
        };
        if (!std::ranges::all_of(residuals, [](double residual) { return std::isfinite(residual); })) {
            return std::numeric_limits<double>::infinity();
        }
        return *std::ranges::max_element(residuals);
    }

    [[nodiscard]] bool within(const ParticleBalanceTolerance2& tolerance = {}) const
    {
        return closes_balance(mass_status) && closes_balance(linear_momentum_status) &&
               closes_balance(angular_momentum_status) && closes_balance(kinetic_energy_status) &&
               maximum_normalized_residual(tolerance) <= 1.0;
    }
};

}  // namespace principia::conservation
