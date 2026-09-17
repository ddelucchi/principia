#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/math/vector.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace principia::operators {

struct OperatorIdTag;
using OperatorId = core::StrongId<OperatorIdTag, std::uint64_t>;

enum class CompositionPolicy : std::uint8_t {
    Additive,
    Multiplicative,
    ReplaceByPriority,
    TransformChain,
    ConstraintResolved,
};

[[nodiscard]] constexpr bool valid_composition_policy(CompositionPolicy policy) noexcept
{
    switch (policy) {
    case CompositionPolicy::Additive:
    case CompositionPolicy::Multiplicative:
    case CompositionPolicy::ReplaceByPriority:
    case CompositionPolicy::TransformChain:
    case CompositionPolicy::ConstraintResolved:
        return true;
    }
    return false;
}

struct CircularRegion2 {
    spacetime::WorldPosition<2> center;
    units::Length radius;

    [[nodiscard]] bool contains(const spacetime::WorldPosition<2>& position) const;
};

struct RotateGravityQuarterTurns {
    std::int8_t counterclockwise_quarter_turns{1};
};

struct ScaleGravity {
    double dimensionless_factor{1.0};
};

struct OffsetGravity {
    fields::GravityVector<2> offset;
};

using GravityTransform = std::variant<RotateGravityQuarterTurns, ScaleGravity, OffsetGravity>;

struct GravityOperatorNode {
    OperatorId id;
    std::int32_t priority{};
    CircularRegion2 support;
    GravityTransform transform;
};

enum class GravityOperatorRestoreError : std::uint8_t {
    InvalidRevision,
    InvalidNode,
    DuplicateIdentifier,
    RevisionExhausted,
};

class GravityOperatorGraph {
public:
    explicit GravityOperatorGraph(CompositionPolicy policy = CompositionPolicy::TransformChain) : policy_(policy)
    {
        if (!valid_composition_policy(policy_)) {
            throw std::invalid_argument("gravity composition policy is invalid");
        }
    }

    [[nodiscard]] bool add(GravityOperatorNode node);
    [[nodiscard]] bool remove(OperatorId id);
    // Persistence-only transactional import. Runtime mutation continues to use
    // add/remove; this API exists solely to retain a historical source stamp.
    [[nodiscard]] std::expected<void, GravityOperatorRestoreError> restore(
        CompositionPolicy policy,
        std::uint64_t revision,
        std::vector<GravityOperatorNode> nodes);
    [[nodiscard]] CompositionPolicy composition_policy() const noexcept { return policy_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::span<const GravityOperatorNode> ordered_nodes() const noexcept { return nodes_; }
    [[nodiscard]] fields::GravityVector<2> apply(
        fields::GravityVector<2> base,
        const spacetime::Event<2>& event) const;

private:
    [[nodiscard]] bool is_legal(const GravityTransform& transform) const noexcept;
    static void apply_transform(fields::GravityVector<2>& value, const GravityTransform& transform);

    CompositionPolicy policy_;
    std::vector<GravityOperatorNode> nodes_;
    std::uint64_t revision_{};
};

class OperatedGravityField2 final : public fields::EffectiveGravityField<2> {
public:
    OperatedGravityField2(
        std::unique_ptr<const fields::EffectiveGravityField<2>> base,
        GravityOperatorGraph operators);

    [[nodiscard]] fields::GravityVector<2> sample(const spacetime::Event<2>& event) const override;
    [[nodiscard]] const fields::FieldMetadata& metadata() const noexcept override { return metadata_; }
    [[nodiscard]] std::uint64_t revision() const noexcept override { return operators_.revision(); }
    [[nodiscard]] const GravityOperatorGraph& operator_graph() const noexcept { return operators_; }
    [[nodiscard]] bool install_operator(GravityOperatorNode node);
    [[nodiscard]] bool remove_operator(OperatorId id);

private:
    std::unique_ptr<const fields::EffectiveGravityField<2>> base_;
    GravityOperatorGraph operators_;
    fields::FieldMetadata metadata_;
};

}  // namespace principia::operators
