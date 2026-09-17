#pragma once

#include <principia/math/vector.hpp>
#include <principia/spacetime/time.hpp>
#include <principia/units/quantity.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace principia::diagnostics {

enum class ValidityReasonCode : std::uint8_t {
    None,
    SpeedRatioExceeded,
    WeakFieldExceeded,
    ContinuumBreakdown,
    UnsupportedState,
};

enum class ValidityState : std::uint8_t {
    WithinDomain,
    NearLimit,
    OutsideDomainNoFallback,
    UnsupportedState,
    UnevaluatedMissingMetric,
};

struct ValidityReport {
    bool valid{true};
    std::string theory;
    std::string criterion;
    ValidityState state{ValidityState::WithinDomain};
    double observed{};
    double limit{};
    std::optional<std::uint64_t> worst_particle_id;
    double estimated_model_error{};
    ValidityReasonCode dominant_violation{ValidityReasonCode::None};
};

enum class EstimateStatus : std::uint8_t {
    NotEstimated,
    Estimated,
};

struct ErrorEstimate {
    EstimateStatus integration_status{EstimateStatus::NotEstimated};
    double integration_error{};
    double discretization_error{};
    double conservation_residual{};
    double model_error{};
};

struct ConstraintRecord2 {
    std::uint64_t particle_id{};
    units::Length position_residual{units::metres(0.0)};
    units::Velocity velocity_residual{units::metres_per_second(0.0)};
    math::Vector<2, units::Momentum> reaction_impulse{
        units::kilogram_metres_per_second(0.0),
        units::kilogram_metres_per_second(0.0),
    };
    bool projected{};
};

struct ConstraintReport {
    double maximum_residual{};
    bool projected{false};
    bool satisfied{true};
    std::vector<ConstraintRecord2> records;
};

enum class ConservationAuditStatus : std::uint8_t {
    Closed,
    BalancedWithSources,
    Failed,
    NotClaimed,
    MissingRepresentation,
    DeferredUntilFoundationValidation,
};

struct ConservationSummary {
    ConservationAuditStatus mass{ConservationAuditStatus::DeferredUntilFoundationValidation};
    ConservationAuditStatus momentum{ConservationAuditStatus::DeferredUntilFoundationValidation};
    ConservationAuditStatus angular_momentum{ConservationAuditStatus::DeferredUntilFoundationValidation};
    ConservationAuditStatus kinetic_energy{ConservationAuditStatus::DeferredUntilFoundationValidation};
    ConservationAuditStatus energy{ConservationAuditStatus::DeferredUntilFoundationValidation};
};

struct SolverStepDiagnostic {
    std::string solver;
    std::uint64_t tick{};
    spacetime::SimulationTime time{};
    units::Duration dt{units::seconds(0.0)};
    std::size_t bodies{};
    std::size_t field_samples{};
    std::uint32_t substeps{1};
    ErrorEstimate error;
    ValidityReport validity;
    ConstraintReport constraints;
    ConservationSummary conservation;
    std::chrono::nanoseconds wall_time{};
};

class DiagnosticLog {
public:
    void record(SolverStepDiagnostic diagnostic) { records_.push_back(std::move(diagnostic)); }
    [[nodiscard]] std::span<const SolverStepDiagnostic> records() const noexcept { return records_; }
    void clear() noexcept { records_.clear(); }

private:
    std::vector<SolverStepDiagnostic> records_;
};

}  // namespace principia::diagnostics
