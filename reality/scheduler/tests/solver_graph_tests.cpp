#include <principia/scheduler/solver_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using principia::scheduler::ScheduleBuildIssueCode;
using principia::scheduler::ScheduleBuildOptions;
using principia::scheduler::SolverDescriptor;
using principia::scheduler::SolverId;
using principia::state::AccessDescriptor;
using principia::state::ChannelSet;
using principia::state::StateChannelId;

constexpr StateChannelId channel_a{101};
constexpr StateChannelId channel_b{102};

[[nodiscard]] principia::scheduler::SolverExecutionContract complete_contract()
{
    using namespace principia::scheduler;
    return SolverExecutionContract{
        principia::ontology::theory_ids::newtonian_particle_dynamics,
        ValidityContractRevision{1},
        StepContractRevision{1},
        IntegratorContractRevision{1},
        StepControlPolicy::Fixed,
        IntegratorFamily::Symplectic,
        {},
        {},
        {},
        DeterministicNumericPolicy{
            DeterminismGuarantee::BitwiseWithinBuild,
            FloatingPointPolicy::StrictIeee754,
            NonFinitePolicy::RejectProposal,
            IterationOrderPolicy::CanonicalPersistentId,
            ParallelAccumulationPolicy::SerialCanonical,
        },
    };
}

[[nodiscard]] SolverDescriptor solver(
    SolverId id,
    std::string name,
    AccessDescriptor access)
{
    return SolverDescriptor{id, std::move(name), std::move(access), complete_contract()};
}

void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

[[nodiscard]] bool has_issue(
    const principia::scheduler::SolverGraphBuildResult& result,
    ScheduleBuildIssueCode code)
{
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
}

void test_canonical_dependencies_and_layers()
{
    const std::array descriptors{
        solver(SolverId{3}, "sink", AccessDescriptor{ChannelSet{channel_b}, ChannelSet{}}),
        solver(SolverId{1}, "source", AccessDescriptor{ChannelSet{}, ChannelSet{channel_a}}),
        solver(SolverId{2}, "middle", AccessDescriptor{ChannelSet{channel_a}, ChannelSet{channel_b}}),
    };

    const auto result = principia::scheduler::try_build_solver_graph(descriptors);
    require(result.success(), "a simple dependency chain should build");
    const auto& graph = result.graph.value();
    require(graph.solvers().size() == 3U, "all solvers should be retained");
    require(graph.solvers()[0].id == SolverId{1} && graph.solvers()[2].id == SolverId{3},
            "solver order should be canonical by id");
    require(graph.dependencies().size() == 2U, "writer-to-reader dependencies should be inferred");
    require(graph.dependencies()[0].writer == SolverId{1} && graph.dependencies()[0].reader == SolverId{2},
            "the first inferred edge should be source to middle");
    require(graph.dependencies()[1].writer == SolverId{2} && graph.dependencies()[1].reader == SolverId{3},
            "the second inferred edge should be middle to sink");
    require(graph.topological_layers().size() == 3U, "a three-node chain should have three layers");
    require(!graph.contains_cycles(), "a chain should remain acyclic");
}

void test_cycle_is_a_coupled_component()
{
    const std::array descriptors{
        solver(SolverId{2}, "right", AccessDescriptor{ChannelSet{channel_a}, ChannelSet{channel_b}}),
        solver(SolverId{1}, "left", AccessDescriptor{ChannelSet{channel_b}, ChannelSet{channel_a}}),
    };

    const auto graph = principia::scheduler::build_solver_graph(descriptors);
    require(graph.contains_cycles(), "mutual data dependencies should remain visible as a cycle");
    require(graph.strongly_connected_components().size() == 1U, "a two-node cycle should collapse to one SCC");
    const auto& component = graph.strongly_connected_components().front();
    require(component.coupled() && component.solvers == std::vector{SolverId{1}, SolverId{2}},
            "coupled SCC members should be canonically ordered");
    require(graph.condensation_edges().empty(), "an isolated SCC has no condensation edge");
    require(graph.topological_layers().size() == 1U, "an isolated SCC should occupy one schedulable layer");
}

void test_write_conflicts_require_reducer_metadata()
{
    const std::array descriptors{
        solver(SolverId{2}, "second", AccessDescriptor{ChannelSet{}, ChannelSet{channel_a}}),
        solver(SolverId{1}, "first", AccessDescriptor{ChannelSet{}, ChannelSet{channel_a}}),
    };

    const auto rejected = principia::scheduler::try_build_solver_graph(descriptors);
    require(!rejected.success(), "unordered writers should be rejected");
    require(rejected.issues.size() == 1U &&
                rejected.issues.front().code == ScheduleBuildIssueCode::AmbiguousWriteConflict,
            "the rejection should identify an ambiguous write conflict");
    require(rejected.issues.front().channel == channel_a, "the conflicting channel should be reported");
    require(rejected.issues.front().solvers == std::vector{SolverId{1}, SolverId{2}},
            "conflicting writers should be reported canonically");

    const auto accepted = principia::scheduler::try_build_solver_graph(
        descriptors,
        ScheduleBuildOptions{{principia::scheduler::make_channel_reduction_policy<double>(
            channel_a,
            principia::scheduler::ReducerId{1},
            principia::scheduler::ReducerContractRevision{1},
            principia::scheduler::ReductionOperation::Sum,
            principia::scheduler::ReductionOrder::CanonicalSolverIdLeftFold)}});
    require(accepted.success(), "explicit reducer metadata should resolve the write conflict");
    require(accepted.graph->dependencies().empty(), "a reducer should not invent writer ordering");
    require(accepted.graph->reduction_policies().size() == 1U &&
                accepted.graph->find_reduction_policy(channel_a) != nullptr,
            "the executable graph should retain its canonical reduction contract");
    require(accepted.graph->topological_layers().size() == 1U &&
                accepted.graph->topological_layers().front().components.size() == 2U,
            "independent reducer contributors should share a parallel layer");
}

void test_dependency_channels_are_aggregated()
{
    const std::array descriptors{
        solver(SolverId{1}, "source", AccessDescriptor{ChannelSet{}, ChannelSet{channel_b, channel_a}}),
        solver(SolverId{2}, "sink", AccessDescriptor{ChannelSet{channel_b, channel_a}, ChannelSet{}}),
    };
    const auto graph = principia::scheduler::build_solver_graph(descriptors);
    require(graph.dependencies().size() == 1U, "one solver pair should have one canonical edge");
    require(graph.dependencies().front().channels.values().size() == 2U,
            "all inducing channels should be retained on that edge");
    require(graph.dependencies().front().channels.values()[0] == channel_a,
            "edge channels should be canonically ordered");
}

void test_incomplete_solver_contracts_are_rejected()
{
    auto descriptor = solver(SolverId{1}, "incomplete", AccessDescriptor{});
    descriptor.contract = {};
    const std::array descriptors{descriptor};
    const auto incomplete = principia::scheduler::try_build_solver_graph(descriptors);
    require(!incomplete.success(), "a descriptor without an execution contract must not become schedulable");
    require(has_issue(incomplete, ScheduleBuildIssueCode::MissingTheoryId) &&
                has_issue(incomplete, ScheduleBuildIssueCode::MissingValidityContractRevision) &&
                has_issue(incomplete, ScheduleBuildIssueCode::MissingStepContractRevision) &&
                has_issue(incomplete, ScheduleBuildIssueCode::MissingIntegratorContractRevision) &&
                has_issue(incomplete, ScheduleBuildIssueCode::UnspecifiedStepControl) &&
                has_issue(incomplete, ScheduleBuildIssueCode::UnspecifiedIntegrator) &&
                has_issue(incomplete, ScheduleBuildIssueCode::IncompleteNumericPolicy),
            "every missing executable-contract dimension should be diagnosed");

    descriptor = solver(SolverId{1}, "invalid enum", AccessDescriptor{});
    descriptor.contract.numeric.guarantee = static_cast<principia::scheduler::DeterminismGuarantee>(255U);
    const std::array invalid_numeric_descriptors{descriptor};
    const auto invalid_numeric = principia::scheduler::try_build_solver_graph(invalid_numeric_descriptors);
    require(has_issue(invalid_numeric, ScheduleBuildIssueCode::IncompleteNumericPolicy),
            "out-of-range enum representations must not pass as explicit numeric policies");

    descriptor = solver(
        SolverId{1},
        " \t",
        AccessDescriptor{ChannelSet{StateChannelId{}}, ChannelSet{}});
    const std::array malformed_descriptors{descriptor};
    const auto malformed = principia::scheduler::try_build_solver_graph(malformed_descriptors);
    require(has_issue(malformed, ScheduleBuildIssueCode::EmptySolverName) &&
                has_issue(malformed, ScheduleBuildIssueCode::InvalidStateChannelId),
            "blank names and reserved channel IDs should be rejected explicitly");

    descriptor = solver(SolverId{1}, "invalid capabilities", AccessDescriptor{});
    descriptor.contract.claimed_conserved_quantities = {principia::ontology::ConservedQuantityId{}};
    descriptor.contract.constraint_capabilities = {
        {principia::ontology::constraints::fixed_kinematic,
         principia::scheduler::ConstraintCapabilityKind::Projects},
    };
    descriptor.contract.boundary_capabilities = {
        {principia::ontology::boundary_requirements::particle_domain,
         static_cast<principia::scheduler::BoundaryCapabilityKind>(128U)},
    };
    const std::array invalid_capability_descriptors{descriptor};
    const auto invalid_capabilities =
        principia::scheduler::try_build_solver_graph(invalid_capability_descriptors);
    require(has_issue(invalid_capabilities, ScheduleBuildIssueCode::InvalidConservedQuantityClaim) &&
                has_issue(invalid_capabilities, ScheduleBuildIssueCode::InvalidConstraintCapability) &&
                has_issue(invalid_capabilities, ScheduleBuildIssueCode::InvalidBoundaryCapability),
            "reserved IDs, opaque projection, and unknown capability bits should all be rejected");
}

void test_contract_claims_are_checked_against_theory()
{
    auto contract = complete_contract();
    contract.claimed_conserved_quantities = {
        principia::ontology::conserved_quantities::mass,
        principia::ontology::conserved_quantities::electric_charge,
        principia::ontology::conserved_quantities::mass,
    };
    contract.constraint_capabilities = {
        {principia::ontology::constraints::positive_density,
         principia::scheduler::ConstraintCapabilityKind::Evaluates},
    };
    contract.boundary_capabilities = {
        {principia::ontology::boundary_requirements::thermal_interface,
         principia::scheduler::BoundaryCapabilityKind::Samples},
    };
    const std::array descriptors{SolverDescriptor{
        SolverId{1},
        "lying contract",
        AccessDescriptor{
            ChannelSet{principia::state::standard_channels::internal_energy_id},
            ChannelSet{},
        },
        contract,
    }};
    const auto channels = principia::state::make_standard_channel_registry();
    const auto rejected = principia::scheduler::try_build_solver_graph(
        descriptors,
        ScheduleBuildOptions{
            {},
            &channels,
            &principia::ontology::standard_theory_registry(),
        });
    require(!rejected.success(), "a contract must agree with the supplied ontology and channel registries");
    require(has_issue(rejected, ScheduleBuildIssueCode::DuplicateConservedQuantityClaim) &&
                has_issue(rejected, ScheduleBuildIssueCode::ConservationClaimOutsideTheory) &&
                has_issue(rejected, ScheduleBuildIssueCode::ConstraintCapabilityOutsideTheory) &&
                has_issue(rejected, ScheduleBuildIssueCode::BoundaryCapabilityOutsideTheory) &&
                has_issue(rejected, ScheduleBuildIssueCode::AccessOutsideTheorySignature),
            "theory-inconsistent claims, capabilities, and access should each be visible");

    contract = complete_contract();
    contract.claimed_conserved_quantities = {
        principia::ontology::conserved_quantities::energy,
        principia::ontology::conserved_quantities::mass,
    };
    contract.constraint_capabilities = {
        {principia::ontology::constraints::fixed_kinematic,
         principia::scheduler::ConstraintCapabilityKind::Evaluates |
             principia::scheduler::ConstraintCapabilityKind::Projects},
    };
    contract.boundary_capabilities = {
        {principia::ontology::boundary_requirements::particle_domain,
         principia::scheduler::BoundaryCapabilityKind::Samples |
             principia::scheduler::BoundaryCapabilityKind::Enforces},
    };
    const std::array valid_descriptors{SolverDescriptor{
        SolverId{1},
        "consistent contract",
        AccessDescriptor{
            ChannelSet{
                principia::state::standard_channels::position_id,
                principia::state::standard_channels::momentum_id,
                principia::state::standard_channels::rest_mass_id,
                principia::state::standard_channels::effective_gravity_id,
            },
            ChannelSet{
                principia::state::standard_channels::position_id,
                principia::state::standard_channels::momentum_id,
            },
        },
        contract,
    }};
    const auto accepted = principia::scheduler::try_build_solver_graph(
        valid_descriptors,
        ScheduleBuildOptions{
            {},
            &channels,
            &principia::ontology::standard_theory_registry(),
        });
    require(accepted.success(), "a registry-consistent executable contract should build");
    const auto& canonical_claims = accepted.graph->solvers().front().contract.claimed_conserved_quantities;
    require(canonical_claims.size() == 2U &&
                canonical_claims.front() == principia::ontology::conserved_quantities::mass,
            "unordered conservation claims should be retained in canonical ID order");
}

void test_reduction_policy_is_complete_and_typed()
{
    const std::array writers{
        solver(SolverId{2}, "second", AccessDescriptor{{}, ChannelSet{channel_a}}),
        solver(SolverId{1}, "first", AccessDescriptor{{}, ChannelSet{channel_a}}),
    };

    const auto incomplete = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{principia::scheduler::ChannelReductionPolicy{.channel = channel_a}}});
    require(!incomplete.success() && has_issue(incomplete, ScheduleBuildIssueCode::MissingReducerId) &&
                has_issue(incomplete, ScheduleBuildIssueCode::MissingReducerContractRevision) &&
                has_issue(incomplete, ScheduleBuildIssueCode::UntypedReduction) &&
                has_issue(incomplete, ScheduleBuildIssueCode::UnspecifiedReductionOperation) &&
                has_issue(incomplete, ScheduleBuildIssueCode::UnspecifiedReductionOrder),
            "a channel marker alone must not masquerade as executable reducer metadata");

    const auto logical_double = principia::scheduler::make_channel_reduction_policy<double>(
        channel_a,
        principia::scheduler::ReducerId{1},
        principia::scheduler::ReducerContractRevision{1},
        principia::scheduler::ReductionOperation::LogicalAnd,
        principia::scheduler::ReductionOrder::CanonicalSolverIdLeftFold);
    const auto incompatible = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{logical_double}});
    require(has_issue(incompatible, ScheduleBuildIssueCode::IncompatibleReductionValueType),
            "logical reducer operations should require a bool value channel");

    auto canonical = principia::scheduler::make_channel_reduction_policy<double>(
        channel_a,
        principia::scheduler::ReducerId{1},
        principia::scheduler::ReducerContractRevision{1},
        principia::scheduler::ReductionOperation::Sum,
        principia::scheduler::ReductionOrder::CanonicalSolverIdLeftFold,
        {SolverId{1}, SolverId{2}});
    const auto unexpected_order = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{canonical}});
    require(has_issue(unexpected_order, ScheduleBuildIssueCode::UnexpectedReductionContributorOrder),
            "canonical orders must not silently retain a contradictory explicit sequence");

    canonical.contributor_order.clear();
    const auto duplicate_policy = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{canonical, canonical}});
    require(has_issue(duplicate_policy, ScheduleBuildIssueCode::DuplicateReductionPolicy),
            "a channel must have exactly one reducer identity");
}

void test_reduction_contributor_order_and_phase_are_explicit()
{
    const std::array writers{
        solver(SolverId{2}, "second", AccessDescriptor{{}, ChannelSet{channel_a}}),
        solver(SolverId{1}, "first", AccessDescriptor{{}, ChannelSet{channel_a}}),
    };
    auto explicit_order = principia::scheduler::make_channel_reduction_policy<double>(
        channel_a,
        principia::scheduler::ReducerId{7},
        principia::scheduler::ReducerContractRevision{3},
        principia::scheduler::ReductionOperation::Custom,
        principia::scheduler::ReductionOrder::ExplicitContributorLeftFold,
        {SolverId{2}, SolverId{1}});
    const auto accepted = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{explicit_order}});
    require(accepted.success(), "an exact explicit contributor permutation should be accepted");
    require(accepted.graph->reduction_policies().front().contributor_order ==
                std::vector{SolverId{2}, SolverId{1}},
            "an intentional noncommutative fold order must not be sorted away");

    explicit_order.contributor_order = {SolverId{1}, SolverId{1}};
    const auto duplicate_contributor = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{explicit_order}});
    require(has_issue(duplicate_contributor, ScheduleBuildIssueCode::InvalidReductionContributorOrder),
            "an explicit fold must name each actual writer exactly once");

    const std::array read_modify_write{
        solver(SolverId{1}, "reader-writer", AccessDescriptor{ChannelSet{channel_a}, ChannelSet{channel_a}}),
        solver(SolverId{2}, "writer", AccessDescriptor{{}, ChannelSet{channel_a}}),
    };
    explicit_order.contributor_order = {SolverId{1}, SolverId{2}};
    const auto missing_phase = principia::scheduler::try_build_solver_graph(
        read_modify_write,
        ScheduleBuildOptions{{explicit_order}});
    require(has_issue(missing_phase, ScheduleBuildIssueCode::ReducedChannelReadByContributor),
            "a contributor that reads the reduced output needs a separate reduction phase");

    const std::array one_writer{
        solver(SolverId{1}, "only", AccessDescriptor{{}, ChannelSet{channel_a}}),
    };
    const auto stale_policy = principia::scheduler::try_build_solver_graph(
        one_writer,
        ScheduleBuildOptions{{explicit_order}});
    require(has_issue(stale_policy, ScheduleBuildIssueCode::UnusedReductionPolicy),
            "stale reducer configuration should fail instead of silently changing semantics");
}

void test_reducer_type_agrees_with_channel_registry()
{
    using Mass = principia::units::Mass;
    const auto channels = principia::state::make_standard_channel_registry();
    const auto mass_channel = principia::state::standard_channels::rest_mass_id;
    const std::array writers{
        solver(SolverId{1}, "first", AccessDescriptor{{}, ChannelSet{mass_channel}}),
        solver(SolverId{2}, "second", AccessDescriptor{{}, ChannelSet{mass_channel}}),
    };
    const auto wrong_type = principia::scheduler::make_channel_reduction_policy<double>(
        mass_channel,
        principia::scheduler::ReducerId{1},
        principia::scheduler::ReducerContractRevision{1},
        principia::scheduler::ReductionOperation::Sum,
        principia::scheduler::ReductionOrder::CanonicalPairwiseTree);
    const auto rejected = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{wrong_type}, &channels, nullptr});
    require(has_issue(rejected, ScheduleBuildIssueCode::IncompatibleReductionValueType),
            "a reducer cannot reinterpret a registered channel's C++ value type");

    const auto right_type = principia::scheduler::make_channel_reduction_policy<Mass>(
        mass_channel,
        principia::scheduler::ReducerId{1},
        principia::scheduler::ReducerContractRevision{1},
        principia::scheduler::ReductionOperation::Sum,
        principia::scheduler::ReductionOrder::CanonicalPairwiseTree);
    const auto accepted = principia::scheduler::try_build_solver_graph(
        writers,
        ScheduleBuildOptions{{right_type}, &channels, nullptr});
    require(accepted.success(), "a typed reducer matching its registered channel should build");
}

void test_deep_dependency_chain_uses_bounded_call_stack()
{
    constexpr std::uint32_t solver_count = 4096U;
    constexpr std::uint32_t first_channel = 10'000U;
    std::vector<SolverDescriptor> descriptors;
    descriptors.reserve(solver_count);
    for (std::uint32_t index = 0; index < solver_count; ++index) {
        const auto reads = index == 0U
                               ? ChannelSet{}
                               : ChannelSet{StateChannelId{first_channel + index - 1U}};
        const auto writes = index + 1U == solver_count
                                ? ChannelSet{}
                                : ChannelSet{StateChannelId{first_channel + index}};
        descriptors.push_back(solver(
            SolverId{index + 1U},
            "deep-chain-" + std::to_string(index + 1U),
            AccessDescriptor{reads, writes}));
    }

    const auto result = principia::scheduler::try_build_solver_graph(descriptors);
    require(result.success(), "a deep acyclic dependency chain should remain schedulable");
    require(result.graph->dependencies().size() == solver_count - 1U &&
                result.graph->strongly_connected_components().size() == solver_count &&
                result.graph->topological_layers().size() == solver_count && !result.graph->contains_cycles(),
            "iterative SCC construction should preserve every node and edge in a deep chain");
}

}  // namespace

int main()
{
    test_canonical_dependencies_and_layers();
    test_cycle_is_a_coupled_component();
    test_write_conflicts_require_reducer_metadata();
    test_dependency_channels_are_aggregated();
    test_incomplete_solver_contracts_are_rejected();
    test_contract_claims_are_checked_against_theory();
    test_reduction_policy_is_complete_and_typed();
    test_reduction_contributor_order_and_phase_are_explicit();
    test_reducer_type_agrees_with_channel_registry();
    test_deep_dependency_chain_uses_bounded_call_stack();
}
