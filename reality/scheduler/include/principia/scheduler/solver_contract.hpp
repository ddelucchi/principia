#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/ontology/theory.hpp>
#include <principia/spacetime/time.hpp>
#include <principia/state/channel.hpp>
#include <principia/units/quantity.hpp>

#include <cstdint>
#include <vector>

namespace principia::scheduler {

// Numerical reports use the same persistent identities as theory claims. A
// scheduler-local ID family would make it possible to "audit" a different
// quantity than the selected theory declares.
using ConstraintId = ontology::ConstraintId;
using ConservedQuantityId = ontology::ConservedQuantityId;

// Contract revisions are persistent compatibility identities, not build
// numbers. Distinct strong types prevent a validity evaluator revision from
// accidentally being supplied as an integrator revision.
struct ValidityContractRevisionTag;
struct StepContractRevisionTag;
struct IntegratorContractRevisionTag;

using ValidityContractRevision = core::StrongId<ValidityContractRevisionTag, std::uint32_t>;
using StepContractRevision = core::StrongId<StepContractRevisionTag, std::uint32_t>;
using IntegratorContractRevision = core::StrongId<IntegratorContractRevisionTag, std::uint32_t>;

enum class StepControlPolicy : std::uint8_t {
    Unspecified,
    Fixed,
    Adaptive,
    Multirate,
};

enum class IntegratorFamily : std::uint8_t {
    Unspecified,
    Exact,
    ExplicitRungeKutta,
    ImplicitRungeKutta,
    LinearMultistep,
    Symplectic,
    Variational,
    OperatorSplit,
    Custom,
};

enum class ConstraintCapabilityKind : std::uint8_t {
    None = 0,
    Evaluates = 1U << 0U,
    Projects = 1U << 1U,
    Solves = 1U << 2U,
};

[[nodiscard]] constexpr ConstraintCapabilityKind operator|(
    ConstraintCapabilityKind left,
    ConstraintCapabilityKind right) noexcept
{
    return static_cast<ConstraintCapabilityKind>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_capability(
    ConstraintCapabilityKind available,
    ConstraintCapabilityKind requested) noexcept
{
    return (static_cast<std::uint8_t>(available) & static_cast<std::uint8_t>(requested)) ==
           static_cast<std::uint8_t>(requested);
}

struct ConstraintCapability {
    ConstraintId id;
    ConstraintCapabilityKind capabilities{ConstraintCapabilityKind::None};

    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const ConstraintCapability&, const ConstraintCapability&) = default;
};

enum class BoundaryCapabilityKind : std::uint8_t {
    None = 0,
    Samples = 1U << 0U,
    Enforces = 1U << 1U,
    AccountsFlux = 1U << 2U,
};

[[nodiscard]] constexpr BoundaryCapabilityKind operator|(
    BoundaryCapabilityKind left,
    BoundaryCapabilityKind right) noexcept
{
    return static_cast<BoundaryCapabilityKind>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_capability(
    BoundaryCapabilityKind available,
    BoundaryCapabilityKind requested) noexcept
{
    return (static_cast<std::uint8_t>(available) & static_cast<std::uint8_t>(requested)) ==
           static_cast<std::uint8_t>(requested);
}

struct BoundaryCapability {
    ontology::BoundaryRequirementId id;
    BoundaryCapabilityKind capabilities{BoundaryCapabilityKind::None};

    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const BoundaryCapability&, const BoundaryCapability&) = default;
};

enum class DeterminismGuarantee : std::uint8_t {
    Unspecified,
    BitwiseAcrossSupportedPlatforms,
    BitwiseWithinBuild,
    DeterministicWithinTolerance,
};

enum class FloatingPointPolicy : std::uint8_t {
    Unspecified,
    StrictIeee754,
    DeclaredContraction,
    ExactArithmetic,
};

enum class NonFinitePolicy : std::uint8_t {
    Unspecified,
    RejectInput,
    RejectProposal,
    FailStep,
};

enum class IterationOrderPolicy : std::uint8_t {
    Unspecified,
    CanonicalPersistentId,
    CanonicalSpatialLexicographic,
    ExplicitStableSequence,
    NotApplicable,
};

enum class ParallelAccumulationPolicy : std::uint8_t {
    Unspecified,
    SerialCanonical,
    // Sort inputs by the solver's declared iteration order, reduce adjacent
    // pairs left-to-right, carry an unpaired final input unchanged, and repeat.
    CanonicalPairwiseTree,
    ExactAccumulator,
    NotApplicable,
};

// Every field is mandatory. In particular, determinism is not inferred from
// today's implementation: changing iteration or accumulation order is a
// contract change even when the mathematical equations are unchanged.
struct DeterministicNumericPolicy {
    DeterminismGuarantee guarantee{DeterminismGuarantee::Unspecified};
    FloatingPointPolicy floating_point{FloatingPointPolicy::Unspecified};
    NonFinitePolicy non_finite{NonFinitePolicy::Unspecified};
    IterationOrderPolicy iteration_order{IterationOrderPolicy::Unspecified};
    ParallelAccumulationPolicy parallel_accumulation{ParallelAccumulationPolicy::Unspecified};
    // Dimensionless normalized tolerance. It must be finite and positive only
    // for DeterministicWithinTolerance; exact bitwise guarantees require zero.
    double normalized_reproducibility_tolerance{};

    [[nodiscard]] bool complete() const noexcept;

    friend bool operator==(const DeterministicNumericPolicy&, const DeterministicNumericPolicy&) = default;
};

// This is the executable interpretation contract attached to a solver graph
// node. Empty capability/claim vectors are valid and mean explicitly "none";
// zero theory/revision IDs and Unspecified policies are not valid contracts.
struct SolverExecutionContract {
    ontology::TheoryId theory;
    ValidityContractRevision validity_revision;
    StepContractRevision step_revision;
    IntegratorContractRevision integrator_revision;
    StepControlPolicy step_control{StepControlPolicy::Unspecified};
    IntegratorFamily integrator{IntegratorFamily::Unspecified};
    ontology::ConservedQuantitySet claimed_conserved_quantities;
    std::vector<ConstraintCapability> constraint_capabilities;
    std::vector<BoundaryCapability> boundary_capabilities;
    DeterministicNumericPolicy numeric;

    [[nodiscard]] bool complete() const noexcept;

    friend bool operator==(const SolverExecutionContract&, const SolverExecutionContract&) = default;
};

// Error components are dimensionless, normalized estimates. Keeping model error
// separate prevents a numerically accurate solve from hiding an invalid model.
struct ErrorEstimate {
    double discretization{};
    double integration{};
    double constraint_residual{};
    double conservation_residual{};
    double model{};

    [[nodiscard]] double numerical() const noexcept;
    [[nodiscard]] double total() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
};

struct ConstraintResidual {
    ConstraintId id;
    double normalized_residual{};
    double tolerance{};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool satisfied() const noexcept;
};

struct ConstraintReport {
    std::vector<ConstraintResidual> residuals;
    bool evaluated{false};
    bool projection_applied{false};

    [[nodiscard]] bool well_formed() const noexcept;
    [[nodiscard]] bool satisfied() const noexcept;
    [[nodiscard]] double worst_normalized_residual() const noexcept;
};

struct ConservationResidual {
    ConservedQuantityId id;
    double normalized_residual{};
    double tolerance{};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool satisfied() const noexcept;
};

struct ConservationReport {
    std::vector<ConservationResidual> residuals;
    bool evaluated{false};
    bool sources_and_fluxes_accounted{false};

    [[nodiscard]] bool well_formed() const noexcept;
    [[nodiscard]] bool satisfied() const noexcept;
    [[nodiscard]] double worst_normalized_residual() const noexcept;
};

// The scheduler does not need to know what a region is. Solvers describe the
// halo depth and the channels whose neighboring values must be available.
struct NeighborDataRequirement {
    std::uint32_t halo_depth{};
    state::ChannelSet channels;

    [[nodiscard]] bool valid() const noexcept;
};

struct SolverProposal {
    units::Duration maximum_stable_step{units::seconds(0.0)};
    units::Duration preferred_step{units::seconds(0.0)};
    ErrorEstimate estimated_error;
    NeighborDataRequirement neighboring_data;
    state::ChannelSet required_boundary_channels;
    std::uint32_t requested_substeps{1};

    [[nodiscard]] bool valid() const noexcept;
};

enum class StepDisposition : std::uint8_t {
    Accepted,
    Rejected,
    Failed,
};

struct StepResult {
    StepDisposition disposition{StepDisposition::Accepted};
    spacetime::SimulationTime ending_time;
    units::Duration advanced{units::seconds(0.0)};
    std::uint32_t substeps{};
    ErrorEstimate error;
    ConstraintReport constraints;
    ConservationReport conservation;

    [[nodiscard]] bool accepted() const noexcept { return disposition == StepDisposition::Accepted; }
    [[nodiscard]] bool valid() const noexcept;
};

}  // namespace principia::scheduler
