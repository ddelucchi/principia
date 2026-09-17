#include <principia/game/progression/learning_graph.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>

namespace principia::game::progression {

namespace {

[[nodiscard]] constexpr bool valid(KnowledgeStage stage) noexcept
{
    switch (stage) {
    case KnowledgeStage::Phenomenon:
    case KnowledgeStage::Measurement:
    case KnowledgeStage::Model:
    case KnowledgeStage::Prediction:
    case KnowledgeStage::Engineering:
    case KnowledgeStage::Manipulation:
    case KnowledgeStage::Unification:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(CausalDepth depth) noexcept
{
    switch (depth) {
    case CausalDepth::State:
    case CausalDepth::Source:
    case CausalDepth::Field:
    case CausalDepth::ConstitutiveResponse:
    case CausalDepth::Boundary:
    case CausalDepth::Coupling:
    case CausalDepth::Geometry:
    case CausalDepth::Symmetry:
    case CausalDepth::LawParameter:
    case CausalDepth::Law:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(LearningNodeStatus status) noexcept
{
    switch (status) {
    case LearningNodeStatus::Available:
    case LearningNodeStatus::Planned:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid(AccessKind access) noexcept
{
    switch (access) {
    case AccessKind::Observe:
    case AccessKind::Modify:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::optional<CausalDepth> required_depth(InterventionKind kind) noexcept
{
    switch (kind) {
    case InterventionKind::State:
        return CausalDepth::State;
    case InterventionKind::Source:
        return CausalDepth::Source;
    case InterventionKind::Field:
        return CausalDepth::Field;
    case InterventionKind::MaterialResponse:
        return CausalDepth::ConstitutiveResponse;
    case InterventionKind::Boundary:
        return CausalDepth::Boundary;
    case InterventionKind::Coupling:
        return CausalDepth::Coupling;
    case InterventionKind::Geometry:
        return CausalDepth::Geometry;
    case InterventionKind::Symmetry:
        return CausalDepth::Symmetry;
    case InterventionKind::LawParameter:
        return CausalDepth::LawParameter;
    case InterventionKind::LawOperator:
        return CausalDepth::Law;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool no_deeper_than(CausalDepth grant, CausalDepth node) noexcept
{
    return static_cast<std::uint8_t>(grant) <= static_cast<std::uint8_t>(node);
}

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value, std::uint32_t distance) noexcept
{
    return (value >> distance) | (value << (32U - distance));
}

// Local, allocation-free SHA-256 keeps graph identities independent of
// std::hash, object representation, locale, host endianness, and third-party
// crypto availability. It is intentionally private: callers persist only the
// resulting LearningGraphFingerprint bytes.
class Sha256 final {
public:
    void append(std::uint8_t byte) noexcept
    {
        block_[block_size_] = byte;
        ++block_size_;
        ++message_size_;
        if (block_size_ == block_.size()) {
            transform();
            block_size_ = 0U;
        }
    }

    void append(std::span<const std::uint8_t> bytes) noexcept
    {
        for (const auto byte : bytes) {
            append(byte);
        }
    }

    [[nodiscard]] std::array<std::uint8_t, LearningGraphFingerprint::digest_size> finish() noexcept
    {
        const auto message_bits = message_size_ * 8U;
        append(0x80U);
        while (block_size_ != 56U) {
            append(0U);
        }
        for (std::uint32_t shift = 56U;; shift -= 8U) {
            append(static_cast<std::uint8_t>((message_bits >> shift) & 0xffU));
            if (shift == 0U) {
                break;
            }
        }

        std::array<std::uint8_t, LearningGraphFingerprint::digest_size> digest{};
        for (std::size_t index = 0; index < state_.size(); ++index) {
            const auto value = state_[index];
            digest[index * 4U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
            digest[index * 4U + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
            digest[index * 4U + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            digest[index * 4U + 3U] = static_cast<std::uint8_t>(value & 0xffU);
        }
        return digest;
    }

private:
    void transform() noexcept
    {
        static constexpr std::array<std::uint32_t, 64U> round_constants{
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        std::array<std::uint32_t, 64U> schedule{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto offset = index * 4U;
            schedule[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                              (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                              (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                              static_cast<std::uint32_t>(block_[offset + 3U]);
        }
        for (std::size_t index = 16U; index < schedule.size(); ++index) {
            const auto previous_15 = schedule[index - 15U];
            const auto sigma0 = rotate_right(previous_15, 7U) ^ rotate_right(previous_15, 18U) ^
                                (previous_15 >> 3U);
            const auto previous_2 = schedule[index - 2U];
            const auto sigma1 = rotate_right(previous_2, 17U) ^ rotate_right(previous_2, 19U) ^
                                (previous_2 >> 10U);
            schedule[index] = schedule[index - 16U] + sigma0 + schedule[index - 7U] + sigma1;
        }

        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice + round_constants[index] + schedule[index];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8U> state_{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    std::array<std::uint8_t, 64U> block_{};
    std::uint64_t message_size_{0U};
    std::size_t block_size_{0U};
};

void append_u32(Sha256& digest, std::uint32_t value) noexcept
{
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        digest.append(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_u64(Sha256& digest, std::uint64_t value) noexcept
{
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        digest.append(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_size(Sha256& digest, std::size_t value) noexcept
{
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    append_u64(digest, static_cast<std::uint64_t>(value));
}

void append_string(Sha256& digest, std::string_view value) noexcept
{
    append_size(digest, value.size());
    for (const auto character : value) {
        digest.append(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
}

template <typename Id>
void append_optional_id(Sha256& digest, const std::optional<Id>& value) noexcept
{
    digest.append(value.has_value() ? 1U : 0U);
    if (value) {
        append_u32(digest, value->value());
    }
}

[[nodiscard]] LearningGraphFingerprint fingerprint_for(std::span<const LearningNode> nodes) noexcept
{
    // Canonical schema v1: fixed domain + NUL, little-endian schema/counts/IDs,
    // length-prefixed raw name bytes, one-byte enum/optional tags. SHA-256
    // emits its standard big-endian digest bytes.
    static constexpr std::string_view domain = "principia.learning-graph.fingerprint";
    Sha256 digest;
    for (const auto character : domain) {
        digest.append(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
    digest.append(0U);
    append_u32(digest, learning_graph_fingerprint_schema_revision);
    append_size(digest, nodes.size());
    for (const auto& node : nodes) {
        append_u32(digest, node.id.value());
        append_string(digest, node.name);
        digest.append(static_cast<std::uint8_t>(node.stage));
        digest.append(static_cast<std::uint8_t>(node.causal_depth));
        digest.append(static_cast<std::uint8_t>(node.status));

        append_size(digest, node.prerequisites.size());
        for (const auto prerequisite : node.prerequisites) {
            append_u32(digest, prerequisite.value());
        }

        append_size(digest, node.grants.size());
        for (const auto& grant : node.grants) {
            append_u32(digest, grant.target.value());
            digest.append(static_cast<std::uint8_t>(grant.kind));
            digest.append(static_cast<std::uint8_t>(grant.access));
            digest.append(static_cast<std::uint8_t>(grant.depth));
            append_optional_id(digest, grant.channel);
            append_optional_id(digest, grant.theory);
        }
    }
    return LearningGraphFingerprint{digest.finish()};
}

}  // namespace

std::expected<LearningGraph, LearningGraphError> LearningGraph::make(std::vector<LearningNode> nodes)
{
    std::ranges::sort(nodes, {}, &LearningNode::id);
    std::set<std::string> names;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (!nodes[index].id) {
            return std::unexpected(LearningGraphError::InvalidId);
        }
        if (!state::has_non_whitespace(nodes[index].name)) {
            return std::unexpected(LearningGraphError::EmptyName);
        }
        if (!valid(nodes[index].stage)) {
            return std::unexpected(LearningGraphError::InvalidKnowledgeStage);
        }
        if (!valid(nodes[index].causal_depth)) {
            return std::unexpected(LearningGraphError::InvalidCausalDepth);
        }
        if (!valid(nodes[index].status)) {
            return std::unexpected(LearningGraphError::InvalidNodeStatus);
        }
        if (index > 0 && nodes[index - 1].id == nodes[index].id) {
            return std::unexpected(LearningGraphError::DuplicateId);
        }
        if (!names.insert(nodes[index].name).second) {
            return std::unexpected(LearningGraphError::DuplicateName);
        }
        std::ranges::sort(nodes[index].prerequisites);
        if (std::ranges::adjacent_find(nodes[index].prerequisites) != nodes[index].prerequisites.end()) {
            return std::unexpected(LearningGraphError::DuplicatePrerequisite);
        }
        for (const auto prerequisite : nodes[index].prerequisites) {
            if (!prerequisite) {
                return std::unexpected(LearningGraphError::InvalidPrerequisite);
            }
            if (!std::ranges::binary_search(nodes, prerequisite, {}, &LearningNode::id)) {
                return std::unexpected(LearningGraphError::DanglingPrerequisite);
            }
        }

        std::ranges::sort(nodes[index].grants);
        if (std::ranges::adjacent_find(nodes[index].grants) != nodes[index].grants.end()) {
            return std::unexpected(LearningGraphError::DuplicateGrant);
        }
        for (const auto& grant : nodes[index].grants) {
            if (!grant.target) {
                return std::unexpected(LearningGraphError::InvalidGrantTarget);
            }
            const auto kind_depth = required_depth(grant.kind);
            if (!kind_depth) {
                return std::unexpected(LearningGraphError::InvalidGrantKind);
            }
            if (!valid(grant.access)) {
                return std::unexpected(LearningGraphError::InvalidGrantAccess);
            }
            if (!valid(grant.depth)) {
                return std::unexpected(LearningGraphError::InvalidGrantDepth);
            }
            if (grant.channel && !*grant.channel) {
                return std::unexpected(LearningGraphError::InvalidGrantChannel);
            }
            if (grant.theory && !*grant.theory) {
                return std::unexpected(LearningGraphError::InvalidGrantTheory);
            }
            if (grant.depth != *kind_depth) {
                return std::unexpected(LearningGraphError::IncompatibleGrantKindDepth);
            }
            if (!no_deeper_than(grant.depth, nodes[index].causal_depth)) {
                return std::unexpected(LearningGraphError::GrantExceedsNodeCausalDepth);
            }
        }
    }

    // Iterative Kahn traversal keeps untrusted or generated learning graphs
    // independent of the process call-stack limit.
    std::map<LearningNodeId, std::size_t> index_for_id;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        index_for_id.emplace(nodes[index].id, index);
    }
    std::vector<std::size_t> remaining_prerequisites(nodes.size());
    std::vector<std::vector<std::size_t>> dependents(nodes.size());
    std::vector<std::size_t> ready;
    ready.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        remaining_prerequisites[index] = nodes[index].prerequisites.size();
        if (remaining_prerequisites[index] == 0U) {
            ready.push_back(index);
        }
        for (const auto prerequisite : nodes[index].prerequisites) {
            dependents[index_for_id.at(prerequisite)].push_back(index);
        }
    }
    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto index = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto dependent : dependents[index]) {
            --remaining_prerequisites[dependent];
            if (remaining_prerequisites[dependent] == 0U) {
                ready.push_back(dependent);
            }
        }
    }
    if (visited != nodes.size()) {
        return std::unexpected(LearningGraphError::DependencyCycle);
    }
    const auto fingerprint = fingerprint_for(nodes);
    return LearningGraph{std::move(nodes), fingerprint};
}

const LearningNode* LearningGraph::find(LearningNodeId id) const noexcept
{
    const auto found = std::ranges::lower_bound(nodes_, id, {}, &LearningNode::id);
    return found != nodes_.end() && found->id == id ? &*found : nullptr;
}

bool LearningGraph::prerequisites_satisfied(LearningNodeId id, const std::set<LearningNodeId>& completed) const
{
    const auto* node = find(id);
    return node != nullptr && std::ranges::all_of(node->prerequisites, [&](LearningNodeId prerequisite) {
        return completed.contains(prerequisite);
    });
}

std::expected<void, UnlockError> CausalAccessState::unlock(const LearningGraph& graph, LearningNodeId node_id)
{
    if (graph_fingerprint_ && *graph_fingerprint_ != graph.fingerprint()) {
        return std::unexpected(UnlockError::GraphMismatch);
    }
    const auto* node = graph.find(node_id);
    if (node == nullptr) {
        return std::unexpected(UnlockError::UnknownNode);
    }
    if (node->status != LearningNodeStatus::Available) {
        return std::unexpected(UnlockError::PlannedNode);
    }
    if (!graph.prerequisites_satisfied(node_id, completed_)) {
        return std::unexpected(UnlockError::MissingPrerequisite);
    }
    if (completed_.contains(node_id)) {
        return {};
    }

    // Commit both facets together. Besides providing a strong exception
    // guarantee, caching grants here prevents a later query against a different
    // graph from reinterpreting a completed node as stronger authority.
    auto next_fingerprint = graph_fingerprint_;
    auto next_completed = completed_;
    auto next_permissions = permissions_;
    next_fingerprint = graph.fingerprint();
    next_completed.insert(node_id);
    next_permissions.insert(next_permissions.end(), node->grants.begin(), node->grants.end());
    std::ranges::sort(next_permissions);
    next_permissions.erase(std::ranges::unique(next_permissions).begin(), next_permissions.end());
    graph_fingerprint_.swap(next_fingerprint);
    completed_.swap(next_completed);
    permissions_.swap(next_permissions);
    return {};
}

bool CausalAccessState::permits_exact(const CausalPermission& permission) const noexcept
{
    return std::ranges::binary_search(permissions_, permission);
}

bool CausalAccessState::permits_any_variant(InterventionTargetId target, AccessKind access) const noexcept
{
    return std::ranges::any_of(permissions_, [&](const CausalPermission& permission) {
        return permission.target == target && permission.access == access;
    });
}

LearningGraph make_standard_learning_graph()
{
    using enum AccessKind;
    using enum KnowledgeStage;
    using enum LearningNodeStatus;

    auto graph = LearningGraph::make({
        {learning_nodes::falling_phenomenon,
         "Falling objects",
         Phenomenon,
         CausalDepth::State,
         Available,
         {},
         {{intervention_targets::particle_position, InterventionKind::State, Observe, CausalDepth::State,
           state::standard_channels::position_id, std::nullopt}}},
        {learning_nodes::measure_acceleration,
         "Measure gravitational acceleration",
         Measurement,
         CausalDepth::Field,
         Available,
         {learning_nodes::falling_phenomenon},
         {{intervention_targets::particle_momentum, InterventionKind::State, Observe, CausalDepth::State,
           state::standard_channels::momentum_id, std::nullopt},
          {intervention_targets::effective_gravity_field, InterventionKind::Field, Observe, CausalDepth::Field,
           state::standard_channels::effective_gravity_id, ontology::theory_ids::effective_newtonian_gravity_field}}},
        {learning_nodes::newtonian_model,
         "Newtonian particle model",
         Model,
         CausalDepth::Field,
         Available,
         {learning_nodes::measure_acceleration},
         {{intervention_targets::effective_gravity_field, InterventionKind::Field, Observe, CausalDepth::Field,
           state::standard_channels::effective_gravity_id, ontology::theory_ids::newtonian_particle_dynamics}}},
        {learning_nodes::ballistic_prediction,
         "Ballistic prediction",
         Prediction,
         CausalDepth::Field,
         Available,
         {learning_nodes::newtonian_model},
         {}},
        {learning_nodes::field_instrumentation,
         "Effective gravity field instrument",
         Engineering,
         CausalDepth::Field,
         Available,
         {learning_nodes::ballistic_prediction},
         {{intervention_targets::effective_gravity_field, InterventionKind::Field, Observe, CausalDepth::Field,
           state::standard_channels::effective_gravity_id, ontology::theory_ids::effective_newtonian_gravity_field}}},
        {learning_nodes::effective_gravity_manipulation,
         "Effective gravity manipulation",
         Manipulation,
         CausalDepth::Field,
         Available,
         {learning_nodes::field_instrumentation},
         {{intervention_targets::effective_gravity_field, InterventionKind::Field, Modify, CausalDepth::Field,
           state::standard_channels::effective_gravity_id, ontology::theory_ids::effective_newtonian_gravity_field}}},
        {learning_nodes::geometry_unification,
         "Gravity as geometry",
         Unification,
         CausalDepth::Geometry,
         Planned,
         {learning_nodes::effective_gravity_manipulation},
         {{intervention_targets::spacetime_geometry, InterventionKind::Geometry, Observe, CausalDepth::Geometry,
           state::standard_channels::metric_field_id, ontology::theory_ids::general_relativity}}},
    });
    if (!graph) {
        throw std::logic_error("the built-in learning graph is invalid");
    }
    return std::move(*graph);
}

CausalAccessState make_opening_access(const LearningGraph& graph)
{
    CausalAccessState access;
    if (!access.unlock(graph, learning_nodes::falling_phenomenon)) {
        throw std::logic_error("opening access could not unlock its first phenomenon");
    }
    return access;
}

CausalAccessState make_reality_test_access(const LearningGraph& graph)
{
    CausalAccessState access;
    for (const auto node : {
             learning_nodes::falling_phenomenon,
             learning_nodes::measure_acceleration,
             learning_nodes::newtonian_model,
             learning_nodes::ballistic_prediction,
             learning_nodes::field_instrumentation,
             learning_nodes::effective_gravity_manipulation,
         }) {
        if (!access.unlock(graph, node)) {
            throw std::logic_error("Reality Test access profile is inconsistent with the learning graph");
        }
    }
    return access;
}

}  // namespace principia::game::progression
