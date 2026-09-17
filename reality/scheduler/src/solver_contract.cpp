#include <principia/scheduler/solver_contract.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace principia::scheduler {
namespace {

[[nodiscard]] bool valid_estimate(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

template <typename Residual>
[[nodiscard]] double worst_residual(const std::vector<Residual>& residuals) noexcept
{
    double worst = 0.0;
    for (const auto& residual : residuals) {
        if (!residual.valid()) {
            return std::numeric_limits<double>::infinity();
        }
        worst = std::max(worst, residual.normalized_residual);
    }
    return worst;
}

template <typename Residual>
[[nodiscard]] bool residuals_well_formed(const std::vector<Residual>& residuals) noexcept
{
    for (std::size_t index = 0; index < residuals.size(); ++index) {
        if (!residuals[index].valid()) {
            return false;
        }
        if (index != 0U && !(residuals[index - 1U].id < residuals[index].id)) {
            return false;
        }
    }
    return true;
}

template <typename Entry>
[[nodiscard]] bool unique_valid_ids(const std::vector<Entry>& entries) noexcept
{
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!entries[index].id) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (entries[index].id == entries[previous].id) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool valid_channel_set(const state::ChannelSet& channels) noexcept
{
    return std::ranges::all_of(channels.values(), [](state::StateChannelId channel) {
        return static_cast<bool>(channel);
    });
}

}  // namespace

bool ConstraintCapability::valid() const noexcept
{
    constexpr auto known_bits = static_cast<std::uint8_t>(ConstraintCapabilityKind::Evaluates) |
                                static_cast<std::uint8_t>(ConstraintCapabilityKind::Projects) |
                                static_cast<std::uint8_t>(ConstraintCapabilityKind::Solves);
    const auto bits = static_cast<std::uint8_t>(capabilities);
    const auto changes_state = has_capability(capabilities, ConstraintCapabilityKind::Projects) ||
                               has_capability(capabilities, ConstraintCapabilityKind::Solves);
    return static_cast<bool>(id) && bits != 0U && (bits & static_cast<std::uint8_t>(~known_bits)) == 0U &&
           (!changes_state || has_capability(capabilities, ConstraintCapabilityKind::Evaluates));
}

bool BoundaryCapability::valid() const noexcept
{
    constexpr auto known_bits = static_cast<std::uint8_t>(BoundaryCapabilityKind::Samples) |
                                static_cast<std::uint8_t>(BoundaryCapabilityKind::Enforces) |
                                static_cast<std::uint8_t>(BoundaryCapabilityKind::AccountsFlux);
    const auto bits = static_cast<std::uint8_t>(capabilities);
    return static_cast<bool>(id) && bits != 0U &&
           (bits & static_cast<std::uint8_t>(~known_bits)) == 0U;
}

bool DeterministicNumericPolicy::complete() const noexcept
{
    const auto valid_guarantee = [this] {
        switch (guarantee) {
        case DeterminismGuarantee::BitwiseAcrossSupportedPlatforms:
        case DeterminismGuarantee::BitwiseWithinBuild:
        case DeterminismGuarantee::DeterministicWithinTolerance:
            return true;
        case DeterminismGuarantee::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_floating_point = [this] {
        switch (floating_point) {
        case FloatingPointPolicy::StrictIeee754:
        case FloatingPointPolicy::DeclaredContraction:
        case FloatingPointPolicy::ExactArithmetic:
            return true;
        case FloatingPointPolicy::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_non_finite = [this] {
        switch (non_finite) {
        case NonFinitePolicy::RejectInput:
        case NonFinitePolicy::RejectProposal:
        case NonFinitePolicy::FailStep:
            return true;
        case NonFinitePolicy::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_iteration_order = [this] {
        switch (iteration_order) {
        case IterationOrderPolicy::CanonicalPersistentId:
        case IterationOrderPolicy::CanonicalSpatialLexicographic:
        case IterationOrderPolicy::ExplicitStableSequence:
        case IterationOrderPolicy::NotApplicable:
            return true;
        case IterationOrderPolicy::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_accumulation = [this] {
        switch (parallel_accumulation) {
        case ParallelAccumulationPolicy::SerialCanonical:
        case ParallelAccumulationPolicy::CanonicalPairwiseTree:
        case ParallelAccumulationPolicy::ExactAccumulator:
        case ParallelAccumulationPolicy::NotApplicable:
            return true;
        case ParallelAccumulationPolicy::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_tolerance = std::isfinite(normalized_reproducibility_tolerance) &&
                                 (guarantee == DeterminismGuarantee::DeterministicWithinTolerance
                                      ? normalized_reproducibility_tolerance > 0.0
                                      : normalized_reproducibility_tolerance == 0.0);
    return valid_guarantee && valid_floating_point && valid_non_finite && valid_iteration_order &&
           valid_accumulation && valid_tolerance;
}

bool SolverExecutionContract::complete() const noexcept
{
    const auto valid_step_control = [this] {
        switch (step_control) {
        case StepControlPolicy::Fixed:
        case StepControlPolicy::Adaptive:
        case StepControlPolicy::Multirate:
            return true;
        case StepControlPolicy::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_integrator = [this] {
        switch (integrator) {
        case IntegratorFamily::Exact:
        case IntegratorFamily::ExplicitRungeKutta:
        case IntegratorFamily::ImplicitRungeKutta:
        case IntegratorFamily::LinearMultistep:
        case IntegratorFamily::Symplectic:
        case IntegratorFamily::Variational:
        case IntegratorFamily::OperatorSplit:
        case IntegratorFamily::Custom:
            return true;
        case IntegratorFamily::Unspecified:
            return false;
        }
        return false;
    }();
    const auto valid_claims = std::ranges::all_of(claimed_conserved_quantities, [](ConservedQuantityId claim) {
        return static_cast<bool>(claim);
    });
    bool unique_claims = true;
    for (std::size_t index = 0; index < claimed_conserved_quantities.size(); ++index) {
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (claimed_conserved_quantities[index] == claimed_conserved_quantities[previous]) {
                unique_claims = false;
            }
        }
    }
    const auto valid_constraints =
        unique_valid_ids(constraint_capabilities) &&
        std::ranges::all_of(constraint_capabilities, &ConstraintCapability::valid);
    const auto valid_boundaries =
        unique_valid_ids(boundary_capabilities) &&
        std::ranges::all_of(boundary_capabilities, &BoundaryCapability::valid);
    return static_cast<bool>(theory) && static_cast<bool>(validity_revision) && static_cast<bool>(step_revision) &&
           static_cast<bool>(integrator_revision) && valid_step_control && valid_integrator && numeric.complete() &&
           valid_claims && unique_claims && valid_constraints && valid_boundaries;
}

double ErrorEstimate::numerical() const noexcept
{
    if (!valid_estimate(discretization) || !valid_estimate(integration) || !valid_estimate(constraint_residual) ||
        !valid_estimate(conservation_residual)) {
        return std::numeric_limits<double>::infinity();
    }
    return std::max({discretization, integration, constraint_residual, conservation_residual});
}

double ErrorEstimate::total() const noexcept
{
    if (!valid()) {
        return std::numeric_limits<double>::infinity();
    }
    return std::max(numerical(), model);
}

bool ErrorEstimate::valid() const noexcept
{
    return valid_estimate(discretization) && valid_estimate(integration) && valid_estimate(constraint_residual) &&
           valid_estimate(conservation_residual) && valid_estimate(model);
}

bool ConstraintResidual::valid() const noexcept
{
    return static_cast<bool>(id) && valid_estimate(normalized_residual) && valid_estimate(tolerance);
}

bool ConstraintResidual::satisfied() const noexcept
{
    return valid() && normalized_residual <= tolerance;
}

bool ConstraintReport::well_formed() const noexcept
{
    if ((!evaluated && (!residuals.empty() || projection_applied)) ||
        (projection_applied && residuals.empty())) {
        return false;
    }
    return residuals_well_formed(residuals);
}

bool ConstraintReport::satisfied() const noexcept
{
    return evaluated && well_formed() && std::ranges::all_of(residuals, [](const ConstraintResidual& residual) {
               return residual.satisfied();
           });
}

double ConstraintReport::worst_normalized_residual() const noexcept
{
    return well_formed() ? worst_residual(residuals) : std::numeric_limits<double>::infinity();
}

bool ConservationResidual::valid() const noexcept
{
    return static_cast<bool>(id) && valid_estimate(normalized_residual) && valid_estimate(tolerance);
}

bool ConservationResidual::satisfied() const noexcept
{
    return valid() && normalized_residual <= tolerance;
}

bool ConservationReport::well_formed() const noexcept
{
    if (!evaluated && (!residuals.empty() || sources_and_fluxes_accounted)) {
        return false;
    }
    return residuals_well_formed(residuals);
}

bool ConservationReport::satisfied() const noexcept
{
    return evaluated && well_formed() && sources_and_fluxes_accounted && !residuals.empty() &&
           std::ranges::all_of(residuals, [](const ConservationResidual& residual) {
               return residual.satisfied();
           });
}

double ConservationReport::worst_normalized_residual() const noexcept
{
    return well_formed() ? worst_residual(residuals) : std::numeric_limits<double>::infinity();
}

bool NeighborDataRequirement::valid() const noexcept
{
    return valid_channel_set(channels) && ((halo_depth == 0U) == channels.empty());
}

bool SolverProposal::valid() const noexcept
{
    const auto maximum_seconds = units::in_seconds(maximum_stable_step);
    const auto preferred_seconds = units::in_seconds(preferred_step);
    return std::isfinite(maximum_seconds) && maximum_seconds > 0.0 && std::isfinite(preferred_seconds) &&
           preferred_seconds > 0.0 && preferred_seconds <= maximum_seconds && estimated_error.valid() &&
           neighboring_data.valid() && valid_channel_set(required_boundary_channels) && requested_substeps > 0U;
}

bool StepResult::valid() const noexcept
{
    const auto ending_seconds = units::in_seconds(ending_time.elapsed());
    const auto advanced_seconds = units::in_seconds(advanced);
    if (!std::isfinite(ending_seconds) || ending_seconds < 0.0 || !std::isfinite(advanced_seconds) ||
        advanced_seconds < 0.0 || !error.valid() || !constraints.well_formed() || !conservation.well_formed()) {
        return false;
    }
    switch (disposition) {
    case StepDisposition::Accepted:
        return advanced_seconds > 0.0 && ending_seconds >= advanced_seconds && substeps > 0U &&
               (!constraints.evaluated || constraints.satisfied()) &&
               (!conservation.evaluated || conservation.satisfied());
    case StepDisposition::Rejected:
    case StepDisposition::Failed:
        return advanced_seconds == 0.0;
    }
    return false;
}

}  // namespace principia::scheduler
