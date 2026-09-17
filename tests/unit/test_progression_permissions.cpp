#include "../test_support.hpp"

#include <principia/game/progression/learning_graph.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace progression = principia::game::progression;

using progression::AccessKind;
using progression::CausalDepth;
using progression::CausalPermission;
using progression::InterventionKind;
using progression::InterventionTargetId;
using progression::KnowledgeStage;
using progression::LearningGraph;
using progression::LearningGraphError;
using progression::LearningNode;
using progression::LearningNodeId;
using progression::LearningNodeStatus;

[[nodiscard]] std::string fingerprint_hex(const progression::LearningGraphFingerprint& fingerprint)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(progression::LearningGraphFingerprint::digest_size * 2U);
    for (const auto byte : fingerprint.bytes()) {
        result.push_back(digits[static_cast<std::size_t>(byte >> 4U)]);
        result.push_back(digits[static_cast<std::size_t>(byte & 0x0fU)]);
    }
    return result;
}

[[nodiscard]] LearningNode make_node(
    std::vector<CausalPermission> grants,
    CausalDepth depth = CausalDepth::Law)
{
    return {
        LearningNodeId{1},
        "permission fixture",
        KnowledgeStage::Phenomenon,
        depth,
        LearningNodeStatus::Available,
        {},
        std::move(grants),
    };
}

void require_graph_error(
    std::vector<LearningNode> nodes,
    LearningGraphError expected,
    std::string_view message)
{
    const auto graph = LearningGraph::make(std::move(nodes));
    principia::tests::require(!graph, message);
    principia::tests::require(graph.error() == expected, message);
}

void test_exact_matching()
{
    using namespace principia;
    using tests::require;

    const auto graph = progression::make_standard_learning_graph();
    const auto access = progression::make_reality_test_access(graph);
    const CausalPermission granted{
        progression::intervention_targets::effective_gravity_field,
        InterventionKind::Field,
        AccessKind::Modify,
        CausalDepth::Field,
        state::standard_channels::effective_gravity_id,
        ontology::theory_ids::effective_newtonian_gravity_field,
    };
    require(access.permits_exact(granted), "the complete typed permission must match its grant");

    auto wrong = granted;
    wrong.kind = InterventionKind::Source;
    wrong.depth = CausalDepth::Source;
    require(!access.permits_exact(wrong), "an intervention kind must not be treated as a wildcard");

    wrong = granted;
    wrong.depth = CausalDepth::Source;
    require(!access.permits_exact(wrong), "causal depth must participate in exact matching");

    wrong = granted;
    wrong.channel = state::standard_channels::position_id;
    require(!access.permits_exact(wrong), "a different state channel must not inherit authority");

    wrong = granted;
    wrong.theory = ontology::theory_ids::newtonian_particle_dynamics;
    require(!access.permits_exact(wrong), "a permission for one theory must not authorize another");

    wrong = granted;
    wrong.channel.reset();
    require(!access.permits_exact(wrong), "an absent channel is an exact value, not a wildcard");

    wrong = granted;
    wrong.theory.reset();
    require(!access.permits_exact(wrong), "an absent theory is an exact value, not a wildcard");

    wrong = granted;
    wrong.access = AccessKind::Observe;
    require(access.permits_exact(wrong), "the matching observation tuple should remain independently granted");

    const auto opening = progression::make_opening_access(graph);
    require(
        !opening.permits_exact({
            progression::intervention_targets::particle_position,
            InterventionKind::State,
            AccessKind::Modify,
            CausalDepth::State,
            state::standard_channels::position_id,
            std::nullopt}),
        "observation authority must not be promoted to modification authority");

    require(
        access.permits_any_variant(
            progression::intervention_targets::effective_gravity_field,
            AccessKind::Observe),
        "the explicitly broad presentation projection should find a visible variant");
    require(
        !access.permits_any_variant(progression::intervention_targets::spacetime_geometry, AccessKind::Observe),
        "the broad presentation projection must still respect target and access");
}

void test_graph_fingerprint_contract()
{
    using namespace principia;
    using tests::require;

    const auto empty = LearningGraph::make({});
    require(empty.has_value(), "an empty graph remains a valid graph definition");
    require(
        fingerprint_hex(empty->fingerprint()) ==
            "9724afe6a81a124fd5e84d12a73dd8ddaab3f7ea233d7366a32a42f282a5e08b",
        "the graph-fingerprint schema must retain its independently computed canonical v1 digest");

    const CausalPermission first_grant{
        InterventionTargetId{20},
        InterventionKind::State,
        AccessKind::Observe,
        CausalDepth::State,
        state::standard_channels::position_id,
        ontology::theory_ids::classical_state,
    };
    const CausalPermission second_grant{
        InterventionTargetId{21},
        InterventionKind::Field,
        AccessKind::Observe,
        CausalDepth::Field,
        state::standard_channels::effective_gravity_id,
        ontology::theory_ids::effective_newtonian_gravity_field,
    };
    auto first = make_node({second_grant, first_grant});
    first.name = "first fingerprint node";
    auto second = make_node({});
    second.id = LearningNodeId{2};
    second.name = "second fingerprint node";
    second.prerequisites = {LearningNodeId{1}};

    auto graph_a = LearningGraph::make({second, first});
    first.grants = {first_grant, second_grant};
    auto graph_b = LearningGraph::make({first, second});
    require(graph_a.has_value() && graph_b.has_value(), "equivalent graph fixtures should construct");
    require(
        graph_a->fingerprint() == graph_b->fingerprint(),
        "node and grant construction order must not affect the normalized graph identity");

    const auto require_distinct = [&](std::vector<LearningNode> nodes, std::string_view message) {
        const auto changed = LearningGraph::make(std::move(nodes));
        require(changed.has_value(), "each semantic fingerprint mutation fixture must remain valid");
        require(changed->fingerprint() != graph_a->fingerprint(), message);
    };

    auto changed_first = first;
    changed_first.id = LearningNodeId{3};
    auto changed_second = second;
    changed_second.prerequisites = {LearningNodeId{3}};
    require_distinct({changed_first, changed_second}, "persistent node IDs must participate in graph identity");

    changed_first = first;
    changed_first.name += " changed";
    require_distinct({changed_first, second}, "node names must participate in graph identity");

    changed_first = first;
    changed_first.stage = KnowledgeStage::Measurement;
    require_distinct({changed_first, second}, "knowledge stages must participate in graph identity");

    changed_first = first;
    changed_first.causal_depth = CausalDepth::Geometry;
    require_distinct({changed_first, second}, "node causal depths must participate in graph identity");

    changed_first = first;
    changed_first.status = LearningNodeStatus::Planned;
    require_distinct({changed_first, second}, "node availability status must participate in graph identity");

    changed_second = second;
    changed_second.prerequisites.clear();
    require_distinct({first, changed_second}, "prerequisite edges must participate in graph identity");

    changed_first = first;
    changed_first.grants.front().target = InterventionTargetId{22};
    require_distinct({changed_first, second}, "grant targets must participate in graph identity");

    changed_first = first;
    changed_first.grants.front().kind = InterventionKind::Source;
    changed_first.grants.front().depth = CausalDepth::Source;
    require_distinct({changed_first, second}, "grant kind and depth must participate in graph identity");

    changed_first = first;
    changed_first.grants.front().access = AccessKind::Modify;
    require_distinct({changed_first, second}, "grant access kind must participate in graph identity");

    changed_first = first;
    changed_first.grants.front().channel.reset();
    require_distinct({changed_first, second}, "grant channel presence and value must participate in graph identity");

    changed_first = first;
    changed_first.grants.front().theory = ontology::theory_ids::newtonian_particle_dynamics;
    require_distinct({changed_first, second}, "grant theory presence and value must participate in graph identity");

    changed_first = first;
    changed_first.grants.pop_back();
    require_distinct({changed_first, second}, "the number of grants must participate in graph identity");
}

void test_canonical_order_and_graph_bound_authority()
{
    using namespace principia;
    using tests::require;

    const InterventionTargetId target_a{20};
    const InterventionTargetId target_b{21};
    const auto channel = state::standard_channels::position_id;
    const auto theory = ontology::theory_ids::classical_state;

    const CausalPermission a{target_a, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
                             std::nullopt, std::nullopt};
    const CausalPermission b{target_a, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
                             std::nullopt, theory};
    const CausalPermission c{target_a, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
                             channel, std::nullopt};
    const CausalPermission d{target_a, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
                             channel, theory};
    const CausalPermission e{target_a, InterventionKind::State, AccessKind::Modify, CausalDepth::State,
                             std::nullopt, std::nullopt};
    const CausalPermission f{target_a, InterventionKind::Field, AccessKind::Observe, CausalDepth::Field,
                             std::nullopt, std::nullopt};
    const CausalPermission g{target_b, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
                             std::nullopt, std::nullopt};
    const std::vector expected{a, b, c, d, e, f, g};

    auto made = LearningGraph::make({make_node({g, d, f, b, e, c, a})});
    require(made.has_value(), "valid grants in arbitrary input order should construct");
    const auto& ordered = made->ordered_nodes().front().grants;
    require(ordered == expected, "grant order must include target, kind, access, depth, channel, and theory");

    progression::CausalAccessState access;
    require(!access.bound_graph_fingerprint().has_value(), "a fresh access state must be explicitly unbound");
    require(access.unlock(*made, LearningNodeId{1}).has_value(), "the canonical fixture should unlock");
    require(access.belongs_to(*made), "the first successful unlock must bind access to its complete graph identity");
    require(
        std::ranges::equal(access.permissions(), expected),
        "cached access permissions must retain the same complete canonical order");

    auto replacement = LearningGraph::make({make_node({
        {progression::intervention_targets::spacetime_geometry,
         InterventionKind::Geometry,
         AccessKind::Modify,
         CausalDepth::Geometry,
         state::standard_channels::metric_field_id,
         ontology::theory_ids::general_relativity},
    })});
    require(replacement.has_value(), "the alternate graph fixture should be structurally valid");
    const auto completed_before = access.completed_nodes();
    const std::vector permissions_before(access.permissions().begin(), access.permissions().end());
    const auto binding_before = access.bound_graph_fingerprint();
    const auto mismatch = access.unlock(*replacement, LearningNodeId{1});
    require(
        !mismatch && mismatch.error() == progression::UnlockError::GraphMismatch,
        "an already completed numeric ID from a different graph must fail closed rather than appear idempotent");
    require(access.completed_nodes() == completed_before, "a graph mismatch must not change completed nodes");
    require(
        std::ranges::equal(access.permissions(), permissions_before),
        "a graph mismatch must not change cached authority");
    require(
        access.bound_graph_fingerprint() == binding_before,
        "a graph mismatch must not replace the original graph binding");
    require(
        !access.permits_exact({
            progression::intervention_targets::spacetime_geometry,
            InterventionKind::Geometry,
            AccessKind::Modify,
            CausalDepth::Geometry,
            state::standard_channels::metric_field_id,
            ontology::theory_ids::general_relativity}),
        "another graph must not reinterpret an already completed node as new authority");
}

void test_cross_graph_prerequisite_attack_is_rejected()
{
    using namespace principia;
    using tests::require;

    const CausalPermission graph_a_first_permission{
        InterventionTargetId{30}, InterventionKind::State, AccessKind::Observe, CausalDepth::State,
        std::nullopt, std::nullopt};
    const CausalPermission graph_a_second_permission{
        InterventionTargetId{31}, InterventionKind::State, AccessKind::Modify, CausalDepth::State,
        std::nullopt, std::nullopt};
    const CausalPermission foreign_permission{
        InterventionTargetId{32}, InterventionKind::Geometry, AccessKind::Modify, CausalDepth::Geometry,
        state::standard_channels::metric_field_id, ontology::theory_ids::general_relativity};

    auto a_first = make_node({graph_a_first_permission});
    a_first.name = "graph A root";
    auto a_second = make_node({graph_a_second_permission});
    a_second.id = LearningNodeId{2};
    a_second.name = "graph A dependent";
    a_second.prerequisites = {LearningNodeId{1}};
    auto graph_a = LearningGraph::make({a_second, a_first});

    auto b_first = make_node({});
    b_first.name = "graph B root";
    auto b_second = make_node({foreign_permission});
    b_second.id = LearningNodeId{2};
    b_second.name = "graph B privileged dependent";
    b_second.prerequisites = {LearningNodeId{1}};
    auto graph_b = LearningGraph::make({b_first, b_second});
    require(graph_a.has_value() && graph_b.has_value(), "both adversarial graph fixtures should construct");
    require(graph_a->fingerprint() != graph_b->fingerprint(), "different graph definitions need different identities");

    progression::CausalAccessState access;
    require(access.unlock(*graph_a, LearningNodeId{1}).has_value(), "graph A's root should unlock normally");
    const auto foreign_unlock = access.unlock(*graph_b, LearningNodeId{2});
    require(
        !foreign_unlock && foreign_unlock.error() == progression::UnlockError::GraphMismatch,
        "graph A's completed ID must never satisfy graph B's same-numbered prerequisite");
    const auto foreign_unknown = access.unlock(*graph_b, LearningNodeId{999});
    require(
        !foreign_unknown && foreign_unknown.error() == progression::UnlockError::GraphMismatch,
        "a bound state must reject a foreign graph before exposing whether a requested node exists");
    require(
        !access.completed(*graph_b, LearningNodeId{2}),
        "the rejected foreign dependent must remain incomplete in graph-aware queries");
    require(
        !access.completed(*graph_a, LearningNodeId{2}),
        "the rejected numeric ID must also remain incomplete in the bound graph");
    require(!access.permits_exact(foreign_permission), "the rejected foreign dependent must grant no authority");

    require(
        access.unlock(*graph_a, LearningNodeId{2}).has_value(),
        "normal dependent unlocking must continue on the graph to which state is bound");
    require(access.completed(*graph_a, LearningNodeId{2}), "the same-graph dependent should become complete");
    require(
        !access.completed(*graph_b, LearningNodeId{2}),
        "even an equal numeric completed ID must not appear completed in a foreign graph");
    require(
        access.permits_exact(graph_a_second_permission),
        "the same-graph dependent should contribute its exact cached permission");

    auto graph_a_reconstructed = LearningGraph::make({a_first, a_second});
    require(graph_a_reconstructed.has_value(), "a reconstructed canonical graph should construct");
    require(
        access.unlock(*graph_a_reconstructed, LearningNodeId{2}).has_value(),
        "an independently reconstructed but byte-identical canonical graph remains compatible and idempotent");
}

void test_failed_initial_unlock_does_not_poison_binding()
{
    using principia::tests::require;

    auto root = make_node({});
    root.name = "root";
    auto dependent = make_node({});
    dependent.id = LearningNodeId{2};
    dependent.name = "dependent";
    dependent.prerequisites = {LearningNodeId{1}};
    const auto first_graph = LearningGraph::make({root, dependent});

    auto other_root = make_node({});
    other_root.name = "other root";
    const auto other_graph = LearningGraph::make({other_root});
    require(first_graph.has_value() && other_graph.has_value(), "initial-binding fixtures should construct");

    progression::CausalAccessState access;
    const auto missing = access.unlock(*first_graph, LearningNodeId{2});
    require(
        !missing && missing.error() == progression::UnlockError::MissingPrerequisite,
        "a first missing-prerequisite failure should retain its precise error");
    require(
        !access.bound_graph_fingerprint().has_value(),
        "an unsuccessful unlock must not poison a fresh state's graph binding");

    const auto unknown = access.unlock(*first_graph, LearningNodeId{99});
    require(
        !unknown && unknown.error() == progression::UnlockError::UnknownNode,
        "an unknown first node should retain its precise error");
    require(
        !access.bound_graph_fingerprint().has_value(),
        "an unknown-node failure must also leave a fresh state unbound");

    auto planned_root = make_node({});
    planned_root.name = "planned root";
    planned_root.status = LearningNodeStatus::Planned;
    const auto planned_graph = LearningGraph::make({planned_root});
    require(planned_graph.has_value(), "the planned-node binding fixture should construct");
    const auto planned = access.unlock(*planned_graph, LearningNodeId{1});
    require(
        !planned && planned.error() == progression::UnlockError::PlannedNode,
        "a planned first node should retain its precise error");
    require(
        !access.bound_graph_fingerprint().has_value(),
        "a planned-node failure must also leave a fresh state unbound");

    require(
        access.unlock(*other_graph, LearningNodeId{1}).has_value(),
        "a later successful unlock may bind a graph after an earlier failure left state untouched");
    require(access.belongs_to(*other_graph), "the successful graph must own the resulting binding");
}

void test_malformed_graphs()
{
    using namespace principia;

    const CausalPermission valid_grant{
        InterventionTargetId{1},
        InterventionKind::Field,
        AccessKind::Observe,
        CausalDepth::Field,
        state::standard_channels::effective_gravity_id,
        ontology::theory_ids::effective_newtonian_gravity_field,
    };

    auto malformed = valid_grant;
    malformed.target = InterventionTargetId{};
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantTarget,
                        "zero intervention targets must be rejected");

    malformed = valid_grant;
    malformed.channel = state::StateChannelId{};
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantChannel,
                        "present-but-zero channels must be rejected");

    malformed = valid_grant;
    malformed.theory = ontology::TheoryId{};
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantTheory,
                        "present-but-zero theories must be rejected");

    malformed = valid_grant;
    malformed.kind = InterventionKind::Source;
    require_graph_error({make_node({malformed})}, LearningGraphError::IncompatibleGrantKindDepth,
                        "intervention kind and causal depth must describe the same layer");

    require_graph_error({make_node({valid_grant}, CausalDepth::State)},
                        LearningGraphError::GrantExceedsNodeCausalDepth,
                        "a node must not grant access deeper than its own causal understanding");

    require_graph_error({make_node({valid_grant, valid_grant})}, LearningGraphError::DuplicateGrant,
                        "duplicate complete permission tuples must be rejected");

    malformed = valid_grant;
    malformed.kind = static_cast<InterventionKind>(255);
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantKind,
                        "unknown intervention-kind discriminants must be rejected");

    malformed = valid_grant;
    malformed.access = static_cast<AccessKind>(255);
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantAccess,
                        "unknown access-kind discriminants must be rejected");

    malformed = valid_grant;
    malformed.depth = static_cast<CausalDepth>(255);
    require_graph_error({make_node({malformed})}, LearningGraphError::InvalidGrantDepth,
                        "unknown grant-depth discriminants must be rejected");

    auto invalid_node = make_node({});
    invalid_node.causal_depth = static_cast<CausalDepth>(255);
    require_graph_error({invalid_node}, LearningGraphError::InvalidCausalDepth,
                        "unknown node-depth discriminants must be rejected");

    invalid_node = make_node({});
    invalid_node.name.clear();
    require_graph_error({invalid_node}, LearningGraphError::EmptyName,
                        "empty learning-node names must be rejected");

    invalid_node = make_node({});
    invalid_node.name = " \t\r\n";
    require_graph_error({invalid_node}, LearningGraphError::EmptyName,
                        "whitespace-only learning-node names must be rejected");

    auto prerequisite = make_node({});
    prerequisite.id = LearningNodeId{2};
    prerequisite.name = "prerequisite";
    auto dependent = make_node({});
    dependent.prerequisites = {LearningNodeId{2}, LearningNodeId{2}};
    require_graph_error({dependent, prerequisite}, LearningGraphError::DuplicatePrerequisite,
                        "duplicate dependency edges must be rejected rather than silently normalized");
}

void test_deep_graph_validation_is_stack_bounded()
{
    using principia::tests::require;
    constexpr std::uint32_t node_count = 16'384U;
    std::vector<LearningNode> chain;
    chain.reserve(node_count);
    for (std::uint32_t index = 0; index < node_count; ++index) {
        auto node = make_node({});
        node.id = LearningNodeId{index + 1U};
        node.name = "deep-node-" + std::to_string(index + 1U);
        if (index != 0U) {
            node.prerequisites = {LearningNodeId{index}};
        }
        chain.push_back(std::move(node));
    }
    const auto acyclic = LearningGraph::make(chain);
    require(acyclic.has_value(), "a deep acyclic learning chain must not depend on recursive stack depth");

    chain.front().prerequisites = {LearningNodeId{node_count}};
    const auto cyclic = LearningGraph::make(std::move(chain));
    require(!cyclic && cyclic.error() == LearningGraphError::DependencyCycle,
            "a deep dependency cycle must be rejected without recursive traversal");
}

}  // namespace

int main()
{
    try {
        test_exact_matching();
        test_graph_fingerprint_contract();
        test_canonical_order_and_graph_bound_authority();
        test_cross_graph_prerequisite_attack_is_rejected();
        test_failed_initial_unlock_does_not_poison_binding();
        test_malformed_graphs();
        test_deep_graph_validation_is_stack_bounded();
        std::cout << "[PASS] progression_permissions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] progression_permissions: " << error.what() << '\n';
        return 1;
    }
}
