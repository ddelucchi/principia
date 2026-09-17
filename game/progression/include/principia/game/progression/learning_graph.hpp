#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/ontology/theory.hpp>
#include <principia/state/channel.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace principia::game::progression {

struct LearningNodeIdTag;
struct InterventionTargetIdTag;
using LearningNodeId = core::StrongId<LearningNodeIdTag, std::uint32_t>;
using InterventionTargetId = core::StrongId<InterventionTargetIdTag, std::uint32_t>;
inline constexpr std::uint32_t learning_graph_fingerprint_schema_revision = 1U;

// A content identity for the complete, normalized learning-graph definition.
// The bytes are a domain-separated SHA-256 digest over the canonical encoding
// documented by LearningGraph::fingerprint(). Keeping the digest a value type
// makes the binding suitable for persistence and deterministic replay.
class LearningGraphFingerprint {
public:
    static constexpr std::size_t digest_size = 32U;

    constexpr LearningGraphFingerprint() = default;
    explicit constexpr LearningGraphFingerprint(std::array<std::uint8_t, digest_size> bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t, digest_size> bytes() const noexcept
    {
        return bytes_;
    }

    friend constexpr auto operator<=>(const LearningGraphFingerprint&, const LearningGraphFingerprint&) = default;

private:
    std::array<std::uint8_t, digest_size> bytes_{};
};

enum class CausalDepth : std::uint8_t {
    State,
    Source,
    Field,
    ConstitutiveResponse,
    Boundary,
    Coupling,
    Geometry,
    Symmetry,
    LawParameter,
    Law,
};

enum class KnowledgeStage : std::uint8_t {
    Phenomenon,
    Measurement,
    Model,
    Prediction,
    Engineering,
    Manipulation,
    Unification,
};

enum class UniverseLayer : std::uint8_t {
    Physical,
    Effective,
    Perceived,
    Manipulable,
    Rendered,
};

enum class InterventionKind : std::uint8_t {
    State,
    Source,
    Field,
    MaterialResponse,
    Boundary,
    Coupling,
    Geometry,
    Symmetry,
    LawParameter,
    LawOperator,
};

enum class AccessKind : std::uint8_t {
    Observe,
    Modify,
};

enum class LearningNodeStatus : std::uint8_t {
    Available,
    Planned,
};

struct CausalPermission {
    InterventionTargetId target;
    InterventionKind kind{InterventionKind::State};
    AccessKind access{AccessKind::Observe};
    CausalDepth depth{CausalDepth::State};
    std::optional<state::StateChannelId> channel;
    std::optional<ontology::TheoryId> theory;

    friend bool operator==(const CausalPermission&, const CausalPermission&) = default;
    // Member order is the canonical persistence/UI order. Keep every field in
    // this comparison: optional channel and theory values are constraints, not
    // wildcards.
    friend auto operator<=>(const CausalPermission&, const CausalPermission&) = default;
};

struct LearningNode {
    LearningNodeId id;
    std::string name;
    KnowledgeStage stage{KnowledgeStage::Phenomenon};
    CausalDepth causal_depth{CausalDepth::State};
    LearningNodeStatus status{LearningNodeStatus::Planned};
    std::vector<LearningNodeId> prerequisites;
    std::vector<CausalPermission> grants;
};

enum class LearningGraphError : std::uint8_t {
    InvalidId,
    DuplicateId,
    DuplicateName,
    DanglingPrerequisite,
    DependencyCycle,
    EmptyName,
    InvalidKnowledgeStage,
    InvalidCausalDepth,
    InvalidNodeStatus,
    InvalidPrerequisite,
    DuplicatePrerequisite,
    InvalidGrantTarget,
    InvalidGrantKind,
    InvalidGrantAccess,
    InvalidGrantDepth,
    InvalidGrantChannel,
    InvalidGrantTheory,
    IncompatibleGrantKindDepth,
    GrantExceedsNodeCausalDepth,
    DuplicateGrant,
};

class LearningGraph {
public:
    [[nodiscard]] static std::expected<LearningGraph, LearningGraphError> make(std::vector<LearningNode> nodes);

    [[nodiscard]] const LearningNode* find(LearningNodeId id) const noexcept;
    [[nodiscard]] std::span<const LearningNode> ordered_nodes() const noexcept { return nodes_; }

    // Fingerprints are stable across construction order and process/platform
    // boundaries. The canonical v1 encoding includes every semantic node,
    // prerequisite, and grant field after normalization. Any future encoding
    // change must use a new domain/schema revision rather than reinterpreting
    // persisted bytes.
    [[nodiscard]] const LearningGraphFingerprint& fingerprint() const noexcept { return fingerprint_; }

private:
    friend class CausalAccessState;

    [[nodiscard]] bool prerequisites_satisfied(
        LearningNodeId id,
        const std::set<LearningNodeId>& completed) const;
    explicit LearningGraph(std::vector<LearningNode> nodes, LearningGraphFingerprint fingerprint)
        : nodes_(std::move(nodes)), fingerprint_(fingerprint)
    {
    }
    std::vector<LearningNode> nodes_;
    LearningGraphFingerprint fingerprint_;
};

enum class UnlockError : std::uint8_t {
    UnknownNode,
    PlannedNode,
    MissingPrerequisite,
    GraphMismatch,
};

class CausalAccessState {
public:
    [[nodiscard]] std::expected<void, UnlockError> unlock(const LearningGraph& graph, LearningNodeId node);
    [[nodiscard]] bool completed(const LearningGraph& graph, LearningNodeId node) const noexcept
    {
        return belongs_to(graph) && completed_.contains(node);
    }
    // Exact permission checks compare target, intervention kind, access, causal
    // depth, channel, and theory. A disengaged optional matches only another
    // disengaged optional; it is never interpreted as a wildcard.
    [[nodiscard]] bool permits_exact(const CausalPermission& permission) const noexcept;

    // Presentation may ask whether any typed variant is visible or manipulable.
    // This deliberately broad projection must never authorize an intervention.
    [[nodiscard]] bool permits_any_variant(InterventionTargetId target, AccessKind access) const noexcept;

    [[nodiscard]] std::span<const CausalPermission> permissions() const noexcept { return permissions_; }
    // Completed IDs are a persistence/UI projection, not portable authority.
    // They are meaningful only together with bound_graph_fingerprint(); use
    // completed(graph, node) for graph-aware queries.
    [[nodiscard]] const std::set<LearningNodeId>& completed_nodes() const noexcept { return completed_; }
    [[nodiscard]] const std::optional<LearningGraphFingerprint>& bound_graph_fingerprint() const noexcept
    {
        return graph_fingerprint_;
    }
    [[nodiscard]] bool belongs_to(const LearningGraph& graph) const noexcept
    {
        return graph_fingerprint_.has_value() && *graph_fingerprint_ == graph.fingerprint();
    }

private:
    std::optional<LearningGraphFingerprint> graph_fingerprint_;
    std::set<LearningNodeId> completed_;
    std::vector<CausalPermission> permissions_;
};

struct UniverseAccessView {
    // Physical and effective truth are engine-owned. The remaining layers are
    // projections of that truth through player access and presentation policy.
    state::ChannelSet physical_channels;
    std::vector<ontology::TheoryId> active_effective_theories;
    state::ChannelSet perceived_channels;
    std::vector<CausalPermission> manipulable;
    state::ChannelSet rendered_channels;
};

namespace learning_nodes {

inline constexpr LearningNodeId falling_phenomenon{1};
inline constexpr LearningNodeId measure_acceleration{2};
inline constexpr LearningNodeId newtonian_model{3};
inline constexpr LearningNodeId ballistic_prediction{4};
inline constexpr LearningNodeId field_instrumentation{5};
inline constexpr LearningNodeId effective_gravity_manipulation{6};
inline constexpr LearningNodeId geometry_unification{7};

}  // namespace learning_nodes

namespace intervention_targets {

inline constexpr InterventionTargetId particle_position{1};
inline constexpr InterventionTargetId particle_momentum{2};
inline constexpr InterventionTargetId effective_gravity_field{3};
inline constexpr InterventionTargetId spacetime_geometry{4};

}  // namespace intervention_targets

[[nodiscard]] LearningGraph make_standard_learning_graph();
[[nodiscard]] CausalAccessState make_opening_access(const LearningGraph& graph);
[[nodiscard]] CausalAccessState make_reality_test_access(const LearningGraph& graph);

}  // namespace principia::game::progression
