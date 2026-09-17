#include <principia/scheduler/solver_graph.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <typeindex>
#include <utility>

namespace principia::scheduler {
namespace {

using SolverIndex = std::size_t;
using RawComponentIndex = std::size_t;

struct SccData {
    std::vector<StronglyConnectedComponent> components;
    std::vector<CondensationDependency> condensation_edges;
    std::vector<TopologicalLayer> layers;
};

[[nodiscard]] std::string solver_list(std::span<const SolverId> solvers)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < solvers.size(); ++index) {
        if (index != 0U) {
            output << ", ";
        }
        output << solvers[index].value();
    }
    return output.str();
}

[[nodiscard]] std::string issue_summary(std::span<const ScheduleBuildIssue> issues)
{
    std::ostringstream output;
    output << "invalid solver schedule metadata";
    if (!issues.empty()) {
        output << ": ";
        for (std::size_t index = 0; index < issues.size(); ++index) {
            if (index != 0U) {
                output << "; ";
            }
            output << issues[index].message();
        }
    }
    return output.str();
}

void append_solver_issue(
    SolverGraphBuildResult& result,
    ScheduleBuildIssueCode code,
    SolverId solver,
    std::optional<state::StateChannelId> channel = std::nullopt)
{
    result.issues.push_back(ScheduleBuildIssue{code, channel, {solver}});
}

[[nodiscard]] bool valid_step_control(StepControlPolicy policy) noexcept
{
    switch (policy) {
    case StepControlPolicy::Fixed:
    case StepControlPolicy::Adaptive:
    case StepControlPolicy::Multirate:
        return true;
    case StepControlPolicy::Unspecified:
        return false;
    }
    return false;
}

[[nodiscard]] bool valid_integrator(IntegratorFamily family) noexcept
{
    switch (family) {
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
}

[[nodiscard]] bool valid_constraint_capability(const ConstraintCapability& capability) noexcept
{
    return capability.valid();
}

[[nodiscard]] bool valid_boundary_capability(const BoundaryCapability& capability) noexcept
{
    return capability.valid();
}

[[nodiscard]] bool valid_reduction_operation(ReductionOperation operation) noexcept
{
    switch (operation) {
    case ReductionOperation::Sum:
    case ReductionOperation::Product:
    case ReductionOperation::Minimum:
    case ReductionOperation::Maximum:
    case ReductionOperation::LogicalAnd:
    case ReductionOperation::LogicalOr:
    case ReductionOperation::Custom:
        return true;
    case ReductionOperation::Unspecified:
        return false;
    }
    return false;
}

[[nodiscard]] bool valid_reduction_order(ReductionOrder order) noexcept
{
    switch (order) {
    case ReductionOrder::CanonicalSolverIdLeftFold:
    case ReductionOrder::ExplicitContributorLeftFold:
    case ReductionOrder::CanonicalPairwiseTree:
        return true;
    case ReductionOrder::Unspecified:
        return false;
    }
    return false;
}

template <typename Id>
[[nodiscard]] bool contains_id(std::span<const Id> ids, Id sought) noexcept
{
    return std::ranges::find(ids, sought) != ids.end();
}

void validate_solver_metadata(
    SolverDescriptor& solver,
    const ScheduleBuildOptions& options,
    SolverGraphBuildResult& result)
{
    if (solver.name.find_first_not_of(" \t\r\n") == std::string::npos) {
        append_solver_issue(result, ScheduleBuildIssueCode::EmptySolverName, solver.id);
    }

    std::set<state::StateChannelId> accessed_channels;
    accessed_channels.insert(solver.access.reads.values().begin(), solver.access.reads.values().end());
    accessed_channels.insert(solver.access.writes.values().begin(), solver.access.writes.values().end());
    for (const auto channel : accessed_channels) {
        if (!channel) {
            append_solver_issue(result, ScheduleBuildIssueCode::InvalidStateChannelId, solver.id, channel);
        } else if (options.channel_registry != nullptr && options.channel_registry->find(channel) == nullptr) {
            append_solver_issue(result, ScheduleBuildIssueCode::UnknownStateChannel, solver.id, channel);
        }
    }

    auto& contract = solver.contract;
    const ontology::TheoryDescriptor* theory = nullptr;
    if (!contract.theory) {
        append_solver_issue(result, ScheduleBuildIssueCode::MissingTheoryId, solver.id);
    } else if (options.theory_registry != nullptr) {
        theory = options.theory_registry->find(contract.theory);
        if (theory == nullptr) {
            append_solver_issue(result, ScheduleBuildIssueCode::UnknownTheoryId, solver.id);
        }
    }
    if (theory != nullptr) {
        for (const auto channel : solver.access.reads.values()) {
            if (!theory->channels.reads.contains(channel)) {
                append_solver_issue(result, ScheduleBuildIssueCode::AccessOutsideTheorySignature, solver.id, channel);
            }
        }
        for (const auto channel : solver.access.writes.values()) {
            if (!theory->channels.writes.contains(channel)) {
                append_solver_issue(result, ScheduleBuildIssueCode::AccessOutsideTheorySignature, solver.id, channel);
            }
        }
    }
    if (!contract.validity_revision) {
        append_solver_issue(result, ScheduleBuildIssueCode::MissingValidityContractRevision, solver.id);
    }
    if (!contract.step_revision) {
        append_solver_issue(result, ScheduleBuildIssueCode::MissingStepContractRevision, solver.id);
    }
    if (!contract.integrator_revision) {
        append_solver_issue(result, ScheduleBuildIssueCode::MissingIntegratorContractRevision, solver.id);
    }
    if (!valid_step_control(contract.step_control)) {
        append_solver_issue(result, ScheduleBuildIssueCode::UnspecifiedStepControl, solver.id);
    }
    if (!valid_integrator(contract.integrator)) {
        append_solver_issue(result, ScheduleBuildIssueCode::UnspecifiedIntegrator, solver.id);
    }
    if (!contract.numeric.complete()) {
        append_solver_issue(result, ScheduleBuildIssueCode::IncompleteNumericPolicy, solver.id);
    }

    auto& claims = contract.claimed_conserved_quantities;
    std::ranges::sort(claims);
    bool invalid_claim_reported = false;
    bool duplicate_claim_reported = false;
    bool outside_claim_reported = false;
    for (std::size_t index = 0; index < claims.size(); ++index) {
        const auto claim = claims[index];
        if (!claim && !invalid_claim_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::InvalidConservedQuantityClaim, solver.id);
            invalid_claim_reported = true;
        }
        if (index != 0U && claim == claims[index - 1U] && !duplicate_claim_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::DuplicateConservedQuantityClaim, solver.id);
            duplicate_claim_reported = true;
        }
        if (theory != nullptr && !contains_id<ontology::ConservedQuantityId>(theory->conserved, claim) &&
            !outside_claim_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::ConservationClaimOutsideTheory, solver.id);
            outside_claim_reported = true;
        }
    }

    auto& constraints = contract.constraint_capabilities;
    std::ranges::sort(constraints, [](const auto& left, const auto& right) { return left.id < right.id; });
    bool invalid_constraint_reported = false;
    bool duplicate_constraint_reported = false;
    bool outside_constraint_reported = false;
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        const auto& capability = constraints[index];
        if (!valid_constraint_capability(capability) && !invalid_constraint_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::InvalidConstraintCapability, solver.id);
            invalid_constraint_reported = true;
        }
        if (index != 0U && capability.id == constraints[index - 1U].id && !duplicate_constraint_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::DuplicateConstraintCapability, solver.id);
            duplicate_constraint_reported = true;
        }
        if (theory != nullptr && !contains_id<ontology::ConstraintId>(theory->constraints, capability.id) &&
            !outside_constraint_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::ConstraintCapabilityOutsideTheory, solver.id);
            outside_constraint_reported = true;
        }
    }

    auto& boundaries = contract.boundary_capabilities;
    std::ranges::sort(boundaries, [](const auto& left, const auto& right) { return left.id < right.id; });
    bool invalid_boundary_reported = false;
    bool duplicate_boundary_reported = false;
    bool outside_boundary_reported = false;
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const auto& capability = boundaries[index];
        if (!valid_boundary_capability(capability) && !invalid_boundary_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::InvalidBoundaryCapability, solver.id);
            invalid_boundary_reported = true;
        }
        if (index != 0U && capability.id == boundaries[index - 1U].id && !duplicate_boundary_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::DuplicateBoundaryCapability, solver.id);
            duplicate_boundary_reported = true;
        }
        if (theory != nullptr &&
            !contains_id<ontology::BoundaryRequirementId>(theory->boundaries, capability.id) &&
            !outside_boundary_reported) {
            append_solver_issue(result, ScheduleBuildIssueCode::BoundaryCapabilityOutsideTheory, solver.id);
            outside_boundary_reported = true;
        }
    }
}

[[nodiscard]] SccData compute_sccs(
    std::span<const SolverDescriptor> solvers,
    std::span<const SolverDependency> dependencies)
{
    const auto solver_count = solvers.size();
    std::map<SolverId, SolverIndex> index_for_solver;
    for (SolverIndex index = 0; index < solver_count; ++index) {
        index_for_solver.emplace(solvers[index].id, index);
    }

    std::vector<std::vector<SolverIndex>> adjacency(solver_count);
    std::vector<std::vector<SolverIndex>> reverse_adjacency(solver_count);
    for (const auto& dependency : dependencies) {
        const auto writer = index_for_solver.at(dependency.writer);
        const auto reader = index_for_solver.at(dependency.reader);
        adjacency[writer].push_back(reader);
        reverse_adjacency[reader].push_back(writer);
    }
    for (auto& successors : adjacency) {
        std::ranges::sort(successors);
        successors.erase(std::ranges::unique(successors).begin(), successors.end());
    }
    for (auto& predecessors : reverse_adjacency) {
        std::ranges::sort(predecessors);
        predecessors.erase(std::ranges::unique(predecessors).begin(), predecessors.end());
    }

    // Iterative Kosaraju traversal keeps schedule construction independent of
    // the process call-stack limit even for adversarially deep dependency chains.
    std::vector<bool> visited(solver_count, false);
    std::vector<SolverIndex> finishing_order;
    finishing_order.reserve(solver_count);
    std::vector<std::pair<SolverIndex, std::size_t>> traversal;
    traversal.reserve(solver_count);
    for (SolverIndex start = 0; start < solver_count; ++start) {
        if (visited[start]) {
            continue;
        }
        visited[start] = true;
        traversal.emplace_back(start, 0U);
        while (!traversal.empty()) {
            auto& [node, next_successor] = traversal.back();
            if (next_successor < adjacency[node].size()) {
                const auto successor = adjacency[node][next_successor];
                ++next_successor;
                if (!visited[successor]) {
                    visited[successor] = true;
                    traversal.emplace_back(successor, 0U);
                }
                continue;
            }
            finishing_order.push_back(node);
            traversal.pop_back();
        }
    }

    std::ranges::fill(visited, false);
    std::vector<std::vector<SolverIndex>> raw_components;
    std::vector<SolverIndex> component_traversal;
    component_traversal.reserve(solver_count);
    for (auto order = finishing_order.rbegin(); order != finishing_order.rend(); ++order) {
        const auto start = *order;
        if (visited[start]) {
            continue;
        }
        auto& component = raw_components.emplace_back();
        visited[start] = true;
        component_traversal.push_back(start);
        while (!component_traversal.empty()) {
            const auto node = component_traversal.back();
            component_traversal.pop_back();
            component.push_back(node);
            for (const auto predecessor : reverse_adjacency[node]) {
                if (!visited[predecessor]) {
                    visited[predecessor] = true;
                    component_traversal.push_back(predecessor);
                }
            }
        }
        std::ranges::sort(component);
    }

    std::vector<RawComponentIndex> component_for_solver(solver_count);
    for (RawComponentIndex component_index = 0; component_index < raw_components.size(); ++component_index) {
        for (const auto solver_index : raw_components[component_index]) {
            component_for_solver[solver_index] = component_index;
        }
    }

    std::set<std::pair<RawComponentIndex, RawComponentIndex>> raw_condensation_edges;
    for (const auto& dependency : dependencies) {
        const auto writer_component = component_for_solver[index_for_solver.at(dependency.writer)];
        const auto reader_component = component_for_solver[index_for_solver.at(dependency.reader)];
        if (writer_component != reader_component) {
            raw_condensation_edges.emplace(writer_component, reader_component);
        }
    }

    std::vector<std::vector<RawComponentIndex>> component_successors(raw_components.size());
    std::vector<std::size_t> indegree(raw_components.size(), 0U);
    for (const auto [before, after] : raw_condensation_edges) {
        component_successors[before].push_back(after);
        ++indegree[after];
    }

    const auto component_less = [&raw_components, solvers](RawComponentIndex left, RawComponentIndex right) {
        const auto left_solver = raw_components[left].front();
        const auto right_solver = raw_components[right].front();
        return solvers[left_solver].id < solvers[right_solver].id;
    };

    std::vector<RawComponentIndex> frontier;
    for (RawComponentIndex component_index = 0; component_index < raw_components.size(); ++component_index) {
        if (indegree[component_index] == 0U) {
            frontier.push_back(component_index);
        }
    }
    std::ranges::sort(frontier, component_less);

    SccData result;
    result.components.reserve(raw_components.size());
    std::vector<StronglyConnectedComponentId> canonical_id_for_raw_component(raw_components.size());

    while (!frontier.empty()) {
        TopologicalLayer layer;
        layer.components.reserve(frontier.size());

        for (const auto raw_component : frontier) {
            const auto representation = static_cast<std::uint32_t>(result.components.size() + 1U);
            const StronglyConnectedComponentId component_id{representation};
            canonical_id_for_raw_component[raw_component] = component_id;

            StronglyConnectedComponent component;
            component.id = component_id;
            component.cyclic = raw_components[raw_component].size() > 1U;
            component.solvers.reserve(raw_components[raw_component].size());
            for (const auto solver_index : raw_components[raw_component]) {
                component.solvers.push_back(solvers[solver_index].id);
            }

            result.components.push_back(std::move(component));
            layer.components.push_back(component_id);
        }

        result.layers.push_back(std::move(layer));

        std::vector<RawComponentIndex> next_frontier;
        for (const auto raw_component : frontier) {
            for (const auto successor : component_successors[raw_component]) {
                --indegree[successor];
                if (indegree[successor] == 0U) {
                    next_frontier.push_back(successor);
                }
            }
        }
        std::ranges::sort(next_frontier, component_less);
        frontier = std::move(next_frontier);
    }

    result.condensation_edges.reserve(raw_condensation_edges.size());
    for (const auto [before, after] : raw_condensation_edges) {
        result.condensation_edges.push_back(
            CondensationDependency{canonical_id_for_raw_component[before], canonical_id_for_raw_component[after]});
    }
    std::ranges::sort(result.condensation_edges, [](const auto& left, const auto& right) {
        if (left.before != right.before) {
            return left.before < right.before;
        }
        return left.after < right.after;
    });

    return result;
}

}  // namespace

std::string ScheduleBuildIssue::message() const
{
    const auto solver = [this] {
        return solvers.empty() ? std::string{"<unknown>"} : std::to_string(solvers.front().value());
    };
    const auto channel_name = [this] {
        return channel.has_value() ? std::to_string(channel->value()) : std::string{"<unknown>"};
    };
    switch (code) {
    case ScheduleBuildIssueCode::InvalidSolverId:
        return "solver id 0 is reserved as invalid";
    case ScheduleBuildIssueCode::DuplicateSolverId:
        return "duplicate solver id " + solver();
    case ScheduleBuildIssueCode::EmptySolverName:
        return "solver " + solver() + " has an empty name";
    case ScheduleBuildIssueCode::InvalidStateChannelId:
        return "solver " + solver() + " accesses reserved channel id 0";
    case ScheduleBuildIssueCode::UnknownStateChannel:
        return "solver " + solver() + " accesses unregistered channel " + channel_name();
    case ScheduleBuildIssueCode::AccessOutsideTheorySignature:
        return "solver " + solver() + " accesses channel " + channel_name() +
               " outside its registered theory signature";
    case ScheduleBuildIssueCode::MissingTheoryId:
        return "solver " + solver() + " has no theory id";
    case ScheduleBuildIssueCode::UnknownTheoryId:
        return "solver " + solver() + " names a theory absent from the supplied registry";
    case ScheduleBuildIssueCode::MissingValidityContractRevision:
        return "solver " + solver() + " has no validity-contract revision";
    case ScheduleBuildIssueCode::MissingStepContractRevision:
        return "solver " + solver() + " has no step-contract revision";
    case ScheduleBuildIssueCode::MissingIntegratorContractRevision:
        return "solver " + solver() + " has no integrator-contract revision";
    case ScheduleBuildIssueCode::UnspecifiedStepControl:
        return "solver " + solver() + " has no valid step-control policy";
    case ScheduleBuildIssueCode::UnspecifiedIntegrator:
        return "solver " + solver() + " has no valid integrator family";
    case ScheduleBuildIssueCode::IncompleteNumericPolicy:
        return "solver " + solver() + " has an incomplete deterministic numeric policy";
    case ScheduleBuildIssueCode::InvalidConservedQuantityClaim:
        return "solver " + solver() + " claims reserved conserved-quantity id 0";
    case ScheduleBuildIssueCode::DuplicateConservedQuantityClaim:
        return "solver " + solver() + " repeats a conserved-quantity claim";
    case ScheduleBuildIssueCode::ConservationClaimOutsideTheory:
        return "solver " + solver() + " claims a quantity not conserved by its registered theory";
    case ScheduleBuildIssueCode::InvalidConstraintCapability:
        return "solver " + solver() + " has an invalid constraint capability";
    case ScheduleBuildIssueCode::DuplicateConstraintCapability:
        return "solver " + solver() + " repeats a constraint capability";
    case ScheduleBuildIssueCode::ConstraintCapabilityOutsideTheory:
        return "solver " + solver() + " claims a constraint absent from its registered theory";
    case ScheduleBuildIssueCode::InvalidBoundaryCapability:
        return "solver " + solver() + " has an invalid boundary capability";
    case ScheduleBuildIssueCode::DuplicateBoundaryCapability:
        return "solver " + solver() + " repeats a boundary capability";
    case ScheduleBuildIssueCode::BoundaryCapabilityOutsideTheory:
        return "solver " + solver() + " claims a boundary absent from its registered theory";
    case ScheduleBuildIssueCode::InvalidReductionChannel:
        return "reduction policy names invalid or unregistered channel " + channel_name();
    case ScheduleBuildIssueCode::DuplicateReductionPolicy:
        return "channel " + channel_name() + " has more than one reduction policy";
    case ScheduleBuildIssueCode::MissingReducerId:
        return "channel " + channel_name() + " reduction has no reducer id";
    case ScheduleBuildIssueCode::MissingReducerContractRevision:
        return "channel " + channel_name() + " reduction has no reducer-contract revision";
    case ScheduleBuildIssueCode::UntypedReduction:
        return "channel " + channel_name() + " reduction has no declared C++ value type";
    case ScheduleBuildIssueCode::UnspecifiedReductionOperation:
        return "channel " + channel_name() + " reduction has no valid operation";
    case ScheduleBuildIssueCode::UnspecifiedReductionOrder:
        return "channel " + channel_name() + " reduction has no valid fold order";
    case ScheduleBuildIssueCode::UnexpectedReductionContributorOrder:
        return "channel " + channel_name() + " has contributor IDs for a non-explicit fold order";
    case ScheduleBuildIssueCode::InvalidReductionContributorOrder:
        return "channel " + channel_name() + " explicit fold order does not name every writer exactly once";
    case ScheduleBuildIssueCode::IncompatibleReductionValueType:
        return "channel " + channel_name() + " reduction value type is incompatible with its channel or operation";
    case ScheduleBuildIssueCode::UnusedReductionPolicy:
        return "channel " + channel_name() + " has a reduction policy but fewer than two writers";
    case ScheduleBuildIssueCode::ReducedChannelReadByContributor:
        return "channel " + channel_name() + " is read by reducer contributors [" + solver_list(solvers) +
               "]; a separate reduction phase is required";
    case ScheduleBuildIssueCode::AmbiguousWriteConflict:
        return "channel " + channel_name() + " has unordered writers [" + solver_list(solvers) +
               "] and no typed reduction policy";
    }
    return "unknown schedule build issue";
}

const SolverDescriptor* SolverGraph::find_solver(SolverId id) const noexcept
{
    const auto found = std::ranges::lower_bound(solvers_, id, {}, &SolverDescriptor::id);
    return found != solvers_.end() && found->id == id ? &*found : nullptr;
}

const ChannelReductionPolicy* SolverGraph::find_reduction_policy(state::StateChannelId channel) const noexcept
{
    const auto found =
        std::ranges::lower_bound(reduction_policies_, channel, {}, &ChannelReductionPolicy::channel);
    return found != reduction_policies_.end() && found->channel == channel ? &*found : nullptr;
}

const StronglyConnectedComponent* SolverGraph::find_component(StronglyConnectedComponentId id) const noexcept
{
    const auto found = std::ranges::lower_bound(components_, id, {}, &StronglyConnectedComponent::id);
    return found != components_.end() && found->id == id ? &*found : nullptr;
}

std::optional<StronglyConnectedComponentId> SolverGraph::component_for(SolverId solver) const noexcept
{
    for (const auto& component : components_) {
        if (std::ranges::binary_search(component.solvers, solver)) {
            return component.id;
        }
    }
    return std::nullopt;
}

bool SolverGraph::contains_cycles() const noexcept
{
    return std::ranges::any_of(components_, &StronglyConnectedComponent::cyclic);
}

ScheduleBuildException::ScheduleBuildException(std::vector<ScheduleBuildIssue> issues)
    : std::invalid_argument(issue_summary(issues)), issues_(std::move(issues))
{
}

SolverGraphBuildResult try_build_solver_graph(
    std::span<const SolverDescriptor> descriptors,
    const ScheduleBuildOptions& options)
{
    SolverGraph graph;
    graph.solvers_.assign(descriptors.begin(), descriptors.end());
    std::ranges::sort(graph.solvers_, [](const SolverDescriptor& left, const SolverDescriptor& right) {
        return left.id < right.id;
    });

    SolverGraphBuildResult result;

    bool invalid_id_reported = false;
    for (std::size_t index = 0; index < graph.solvers_.size(); ++index) {
        const auto id = graph.solvers_[index].id;
        if (!id && !invalid_id_reported) {
            result.issues.push_back(ScheduleBuildIssue{
                ScheduleBuildIssueCode::InvalidSolverId,
                std::nullopt,
                {id},
            });
            invalid_id_reported = true;
        }

        if (index != 0U && id == graph.solvers_[index - 1U].id) {
            if (index == 1U || id != graph.solvers_[index - 2U].id) {
                result.issues.push_back(ScheduleBuildIssue{
                    ScheduleBuildIssueCode::DuplicateSolverId,
                    std::nullopt,
                    {id},
                });
            }
        }
    }
    if (!result.issues.empty()) {
        return result;
    }

    for (auto& solver : graph.solvers_) {
        validate_solver_metadata(solver, options, result);
    }

    graph.reduction_policies_ = options.reductions;
    std::ranges::stable_sort(
        graph.reduction_policies_,
        {},
        &ChannelReductionPolicy::channel);

    for (std::size_t index = 0; index < graph.reduction_policies_.size(); ++index) {
        const auto& policy = graph.reduction_policies_[index];
        const auto channel = policy.channel;
        if (!channel ||
            (options.channel_registry != nullptr && options.channel_registry->find(channel) == nullptr)) {
            result.issues.push_back(ScheduleBuildIssue{
                ScheduleBuildIssueCode::InvalidReductionChannel,
                channel,
                {},
            });
        }
        if (index != 0U && channel == graph.reduction_policies_[index - 1U].channel) {
            if (index == 1U || channel != graph.reduction_policies_[index - 2U].channel) {
                result.issues.push_back(ScheduleBuildIssue{
                    ScheduleBuildIssueCode::DuplicateReductionPolicy,
                    channel,
                    {},
                });
            }
        }
        if (!policy.reducer) {
            result.issues.push_back(ScheduleBuildIssue{ScheduleBuildIssueCode::MissingReducerId, channel, {}});
        }
        if (!policy.revision) {
            result.issues.push_back(
                ScheduleBuildIssue{ScheduleBuildIssueCode::MissingReducerContractRevision, channel, {}});
        }
        if (policy.value_type == std::type_index(typeid(void))) {
            result.issues.push_back(ScheduleBuildIssue{ScheduleBuildIssueCode::UntypedReduction, channel, {}});
        }
        if (!valid_reduction_operation(policy.operation)) {
            result.issues.push_back(
                ScheduleBuildIssue{ScheduleBuildIssueCode::UnspecifiedReductionOperation, channel, {}});
        }
        if (!valid_reduction_order(policy.order)) {
            result.issues.push_back(
                ScheduleBuildIssue{ScheduleBuildIssueCode::UnspecifiedReductionOrder, channel, {}});
        }
        if (policy.order != ReductionOrder::ExplicitContributorLeftFold && !policy.contributor_order.empty()) {
            result.issues.push_back(
                ScheduleBuildIssue{ScheduleBuildIssueCode::UnexpectedReductionContributorOrder, channel, {}});
        }

        const auto logical_operation = policy.operation == ReductionOperation::LogicalAnd ||
                                       policy.operation == ReductionOperation::LogicalOr;
        const auto logical_type_mismatch = logical_operation && policy.value_type != std::type_index(typeid(bool));
        const auto* registered_channel =
            options.channel_registry == nullptr ? nullptr : options.channel_registry->find(channel);
        const auto registry_type_mismatch =
            registered_channel != nullptr && registered_channel->value_type != policy.value_type;
        if (logical_type_mismatch || registry_type_mismatch) {
            result.issues.push_back(
                ScheduleBuildIssue{ScheduleBuildIssueCode::IncompatibleReductionValueType, channel, {}});
        }
    }

    std::map<state::StateChannelId, std::vector<SolverId>> writers_by_channel;
    std::map<state::StateChannelId, std::vector<SolverId>> readers_by_channel;
    for (const auto& solver : graph.solvers_) {
        for (const auto channel : solver.access.writes.values()) {
            writers_by_channel[channel].push_back(solver.id);
        }
        for (const auto channel : solver.access.reads.values()) {
            readers_by_channel[channel].push_back(solver.id);
        }
    }

    for (const auto& policy : graph.reduction_policies_) {
        const auto writers_found = writers_by_channel.find(policy.channel);
        if (writers_found == writers_by_channel.end() || writers_found->second.size() < 2U) {
            result.issues.push_back(ScheduleBuildIssue{
                ScheduleBuildIssueCode::UnusedReductionPolicy,
                policy.channel,
                writers_found == writers_by_channel.end() ? std::vector<SolverId>{} : writers_found->second,
            });
            continue;
        }

        const auto& writers = writers_found->second;
        if (policy.order == ReductionOrder::ExplicitContributorLeftFold) {
            auto canonical_contributors = policy.contributor_order;
            std::ranges::sort(canonical_contributors);
            const auto unique_end = std::ranges::unique(canonical_contributors).begin();
            const auto has_duplicate = unique_end != canonical_contributors.end();
            canonical_contributors.erase(unique_end, canonical_contributors.end());
            if (has_duplicate || canonical_contributors != writers) {
                result.issues.push_back(ScheduleBuildIssue{
                    ScheduleBuildIssueCode::InvalidReductionContributorOrder,
                    policy.channel,
                    writers,
                });
            }
        }

        const auto readers_found = readers_by_channel.find(policy.channel);
        if (readers_found != readers_by_channel.end()) {
            std::vector<SolverId> read_write_contributors;
            for (const auto writer : writers) {
                if (std::ranges::binary_search(readers_found->second, writer)) {
                    read_write_contributors.push_back(writer);
                }
            }
            if (!read_write_contributors.empty()) {
                result.issues.push_back(ScheduleBuildIssue{
                    ScheduleBuildIssueCode::ReducedChannelReadByContributor,
                    policy.channel,
                    std::move(read_write_contributors),
                });
            }
        }
    }

    for (const auto& [channel, writers] : writers_by_channel) {
        if (writers.size() > 1U &&
            std::ranges::none_of(graph.reduction_policies_, [channel](const ChannelReductionPolicy& policy) {
                return policy.channel == channel;
            })) {
            result.issues.push_back(ScheduleBuildIssue{
                ScheduleBuildIssueCode::AmbiguousWriteConflict,
                channel,
                writers,
            });
        }
    }
    if (!result.issues.empty()) {
        return result;
    }

    std::map<std::pair<SolverId, SolverId>, std::vector<state::StateChannelId>> channels_by_dependency;
    for (const auto& [channel, writers] : writers_by_channel) {
        const auto readers = readers_by_channel.find(channel);
        if (readers == readers_by_channel.end()) {
            continue;
        }
        for (const auto writer : writers) {
            for (const auto reader : readers->second) {
                if (writer != reader) {
                    channels_by_dependency[{writer, reader}].push_back(channel);
                }
            }
        }
    }

    graph.dependencies_.reserve(channels_by_dependency.size());
    for (auto& [endpoints, channels] : channels_by_dependency) {
        graph.dependencies_.push_back(SolverDependency{
            endpoints.first,
            endpoints.second,
            state::ChannelSet{std::move(channels)},
        });
    }

    auto scc_data = compute_sccs(graph.solvers_, graph.dependencies_);
    graph.components_ = std::move(scc_data.components);
    graph.condensation_edges_ = std::move(scc_data.condensation_edges);
    graph.layers_ = std::move(scc_data.layers);
    result.graph = std::move(graph);
    return result;
}

SolverGraph build_solver_graph(
    std::span<const SolverDescriptor> descriptors,
    const ScheduleBuildOptions& options)
{
    auto result = try_build_solver_graph(descriptors, options);
    if (!result.graph.has_value()) {
        throw ScheduleBuildException{std::move(result.issues)};
    }
    return std::move(result.graph).value();
}

}  // namespace principia::scheduler
