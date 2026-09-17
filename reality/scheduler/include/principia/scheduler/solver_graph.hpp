#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/ontology/registry.hpp>
#include <principia/scheduler/solver_contract.hpp>
#include <principia/state/channel.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

namespace principia::scheduler {

struct SolverIdTag;
using SolverId = core::StrongId<SolverIdTag, std::uint32_t>;

struct StronglyConnectedComponentIdTag;
using StronglyConnectedComponentId = core::StrongId<StronglyConnectedComponentIdTag, std::uint32_t>;

struct SolverDescriptor {
    SolverId id;
    std::string name;
    state::AccessDescriptor access;
    SolverExecutionContract contract;
};

struct ReducerIdTag;
struct ReducerContractRevisionTag;
using ReducerId = core::StrongId<ReducerIdTag, std::uint32_t>;
using ReducerContractRevision = core::StrongId<ReducerContractRevisionTag, std::uint32_t>;

enum class ReductionOperation : std::uint8_t {
    Unspecified,
    Sum,
    Product,
    Minimum,
    Maximum,
    LogicalAnd,
    LogicalOr,
    Custom,
};

enum class ReductionOrder : std::uint8_t {
    Unspecified,
    // Sort contributors by SolverId and evaluate ((first op second) op third)...
    CanonicalSolverIdLeftFold,
    // Use contributor_order exactly and evaluate a left fold in that sequence.
    ExplicitContributorLeftFold,
    // Sort by SolverId; reduce adjacent pairs left-to-right, carry an odd final
    // value unchanged to the next round, and repeat until one value remains.
    CanonicalPairwiseTree,
};

// A reduction is an engine-owned, typed piece of scheduling IR. The reducer ID
// identifies its implementation contract; type_index is used only for runtime
// agreement with the channel registry and is never a persistence identity.
struct ChannelReductionPolicy {
    state::StateChannelId channel;
    ReducerId reducer;
    ReducerContractRevision revision;
    std::type_index value_type{typeid(void)};
    ReductionOperation operation{ReductionOperation::Unspecified};
    ReductionOrder order{ReductionOrder::Unspecified};
    std::vector<SolverId> contributor_order;

    friend bool operator==(const ChannelReductionPolicy&, const ChannelReductionPolicy&) = default;
};

template <typename Value>
[[nodiscard]] ChannelReductionPolicy make_channel_reduction_policy(
    state::StateChannelId channel,
    ReducerId reducer,
    ReducerContractRevision revision,
    ReductionOperation operation,
    ReductionOrder order,
    std::vector<SolverId> contributor_order = {})
{
    return ChannelReductionPolicy{
        channel,
        reducer,
        revision,
        std::type_index(typeid(std::remove_cvref_t<Value>)),
        operation,
        order,
        std::move(contributor_order),
    };
}

// Every multi-writer channel must have exactly one complete reduction policy.
// Optional registries strengthen validation by proving that theory/channel IDs
// exist and that a reducer's declared C++ value type matches its channel.
struct ScheduleBuildOptions {
    std::vector<ChannelReductionPolicy> reductions;
    const state::ChannelRegistry* channel_registry{};
    const ontology::TheoryRegistry* theory_registry{};
};

struct SolverDependency {
    SolverId writer;
    SolverId reader;
    state::ChannelSet channels;

    friend bool operator==(const SolverDependency&, const SolverDependency&) = default;
};

struct StronglyConnectedComponent {
    StronglyConnectedComponentId id;
    std::vector<SolverId> solvers;
    bool cyclic{false};

    [[nodiscard]] bool coupled() const noexcept { return cyclic; }
};

struct CondensationDependency {
    StronglyConnectedComponentId before;
    StronglyConnectedComponentId after;

    friend bool operator==(const CondensationDependency&, const CondensationDependency&) = default;
};

struct TopologicalLayer {
    std::vector<StronglyConnectedComponentId> components;
};

enum class ScheduleBuildIssueCode : std::uint8_t {
    InvalidSolverId,
    DuplicateSolverId,
    EmptySolverName,
    InvalidStateChannelId,
    UnknownStateChannel,
    AccessOutsideTheorySignature,
    MissingTheoryId,
    UnknownTheoryId,
    MissingValidityContractRevision,
    MissingStepContractRevision,
    MissingIntegratorContractRevision,
    UnspecifiedStepControl,
    UnspecifiedIntegrator,
    IncompleteNumericPolicy,
    InvalidConservedQuantityClaim,
    DuplicateConservedQuantityClaim,
    ConservationClaimOutsideTheory,
    InvalidConstraintCapability,
    DuplicateConstraintCapability,
    ConstraintCapabilityOutsideTheory,
    InvalidBoundaryCapability,
    DuplicateBoundaryCapability,
    BoundaryCapabilityOutsideTheory,
    InvalidReductionChannel,
    DuplicateReductionPolicy,
    MissingReducerId,
    MissingReducerContractRevision,
    UntypedReduction,
    UnspecifiedReductionOperation,
    UnspecifiedReductionOrder,
    UnexpectedReductionContributorOrder,
    InvalidReductionContributorOrder,
    IncompatibleReductionValueType,
    UnusedReductionPolicy,
    ReducedChannelReadByContributor,
    AmbiguousWriteConflict,
};

struct ScheduleBuildIssue {
    ScheduleBuildIssueCode code{};
    std::optional<state::StateChannelId> channel;
    std::vector<SolverId> solvers;

    [[nodiscard]] std::string message() const;
};

struct SolverGraphBuildResult;

class SolverGraph {
public:
    SolverGraph() = default;

    [[nodiscard]] std::span<const SolverDescriptor> solvers() const noexcept { return solvers_; }
    [[nodiscard]] std::span<const ChannelReductionPolicy> reduction_policies() const noexcept
    {
        return reduction_policies_;
    }
    [[nodiscard]] std::span<const SolverDependency> dependencies() const noexcept { return dependencies_; }
    [[nodiscard]] std::span<const StronglyConnectedComponent> strongly_connected_components() const noexcept
    {
        return components_;
    }
    [[nodiscard]] std::span<const CondensationDependency> condensation_edges() const noexcept
    {
        return condensation_edges_;
    }
    [[nodiscard]] const std::vector<TopologicalLayer>& topological_layers() const noexcept { return layers_; }

    [[nodiscard]] const SolverDescriptor* find_solver(SolverId id) const noexcept;
    [[nodiscard]] const ChannelReductionPolicy* find_reduction_policy(state::StateChannelId channel) const noexcept;
    [[nodiscard]] const StronglyConnectedComponent* find_component(StronglyConnectedComponentId id) const noexcept;
    [[nodiscard]] std::optional<StronglyConnectedComponentId> component_for(SolverId solver) const noexcept;
    [[nodiscard]] bool contains_cycles() const noexcept;

private:
    friend SolverGraphBuildResult try_build_solver_graph(
        std::span<const SolverDescriptor>,
        const ScheduleBuildOptions&);

    std::vector<SolverDescriptor> solvers_;
    std::vector<ChannelReductionPolicy> reduction_policies_;
    std::vector<SolverDependency> dependencies_;
    std::vector<StronglyConnectedComponent> components_;
    std::vector<CondensationDependency> condensation_edges_;
    std::vector<TopologicalLayer> layers_;
};

struct SolverGraphBuildResult {
    std::optional<SolverGraph> graph;
    std::vector<ScheduleBuildIssue> issues;

    [[nodiscard]] bool success() const noexcept { return graph.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return success(); }
};

class ScheduleBuildException final : public std::invalid_argument {
public:
    explicit ScheduleBuildException(std::vector<ScheduleBuildIssue> issues);

    [[nodiscard]] std::span<const ScheduleBuildIssue> issues() const noexcept { return issues_; }

private:
    std::vector<ScheduleBuildIssue> issues_;
};

// Descriptors, dependencies, SCC members, condensation edges, and layers are
// all returned in canonical order, independent of descriptor registration order.
[[nodiscard]] SolverGraphBuildResult try_build_solver_graph(
    std::span<const SolverDescriptor> descriptors,
    const ScheduleBuildOptions& options = {});

// Convenience form for callers that treat invalid metadata as a configuration
// error. All validation issues are retained by ScheduleBuildException.
[[nodiscard]] SolverGraph build_solver_graph(
    std::span<const SolverDescriptor> descriptors,
    const ScheduleBuildOptions& options = {});

}  // namespace principia::scheduler
