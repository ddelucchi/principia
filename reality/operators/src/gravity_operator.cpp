#include <principia/operators/gravity_operator.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace principia::operators {

namespace {

[[nodiscard]] bool finite_position(const spacetime::WorldPosition<2>& position)
{
    return std::isfinite(units::in_metres(position[0])) && std::isfinite(units::in_metres(position[1]));
}

[[nodiscard]] bool finite_transform(const GravityTransform& transform)
{
    return std::visit(
        [](const auto& operation) {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, RotateGravityQuarterTurns>) {
                return true;
            } else if constexpr (std::same_as<Operation, ScaleGravity>) {
                return std::isfinite(operation.dimensionless_factor);
            } else {
                return std::isfinite(units::in_metres_per_second_squared(operation.offset[0])) &&
                       std::isfinite(units::in_metres_per_second_squared(operation.offset[1]));
            }
        },
        transform);
}

void canonicalize_transform(GravityTransform& transform)
{
    if (auto* rotation = std::get_if<RotateGravityQuarterTurns>(&transform)) {
        auto turns = static_cast<int>(rotation->counterclockwise_quarter_turns) % 4;
        if (turns < 0) {
            turns += 4;
        }
        rotation->counterclockwise_quarter_turns = static_cast<std::int8_t>(turns);
    }
}

}  // namespace

bool CircularRegion2::contains(const spacetime::WorldPosition<2>& position) const
{
    const auto radius_metres = units::in_metres(radius);
    if (!finite_position(center) || !finite_position(position) || !std::isfinite(radius_metres) ||
        radius_metres < 0.0) {
        return false;
    }
    const auto dx = units::in_metres(position[0]) - units::in_metres(center[0]);
    const auto dy = units::in_metres(position[1]) - units::in_metres(center[1]);
    return std::hypot(dx, dy) <= radius_metres;
}

bool GravityOperatorGraph::add(GravityOperatorNode node)
{
    const auto radius = units::in_metres(node.support.radius);
    if (!node.id || revision_ == std::numeric_limits<std::uint64_t>::max() ||
        !is_legal(node.transform) || !finite_transform(node.transform) || !finite_position(node.support.center) ||
        !std::isfinite(radius) || radius < 0.0) {
        return false;
    }
    const auto duplicate = std::ranges::find(nodes_, node.id, &GravityOperatorNode::id);
    if (duplicate != nodes_.end()) {
        return false;
    }
    canonicalize_transform(node.transform);
    nodes_.push_back(std::move(node));
    std::ranges::sort(nodes_, [](const GravityOperatorNode& left, const GravityOperatorNode& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        return left.id < right.id;
    });
    ++revision_;
    return true;
}

bool GravityOperatorGraph::remove(OperatorId id)
{
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto found = std::ranges::find(nodes_, id, &GravityOperatorNode::id);
    if (found == nodes_.end()) {
        return false;
    }
    nodes_.erase(found);
    ++revision_;
    return true;
}

std::expected<void, GravityOperatorRestoreError> GravityOperatorGraph::restore(
    CompositionPolicy policy,
    std::uint64_t revision,
    std::vector<GravityOperatorNode> nodes)
{
    if (!valid_composition_policy(policy)) {
        return std::unexpected(GravityOperatorRestoreError::InvalidNode);
    }
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(GravityOperatorRestoreError::RevisionExhausted);
    }
    if (revision < nodes.size() || ((revision - nodes.size()) % 2U) != 0U) {
        return std::unexpected(GravityOperatorRestoreError::InvalidRevision);
    }

    GravityOperatorGraph restored{policy};
    for (auto& node : nodes) {
        if (std::ranges::find(restored.nodes_, node.id, &GravityOperatorNode::id) != restored.nodes_.end()) {
            return std::unexpected(GravityOperatorRestoreError::DuplicateIdentifier);
        }
        if (!restored.add(std::move(node))) {
            return std::unexpected(GravityOperatorRestoreError::InvalidNode);
        }
    }
    restored.revision_ = revision;
    *this = std::move(restored);
    return {};
}

bool GravityOperatorGraph::is_legal(const GravityTransform& transform) const noexcept
{
    switch (policy_) {
    case CompositionPolicy::Additive:
        return std::holds_alternative<OffsetGravity>(transform);
    case CompositionPolicy::Multiplicative:
        return std::holds_alternative<ScaleGravity>(transform);
    case CompositionPolicy::TransformChain:
        return std::holds_alternative<RotateGravityQuarterTurns>(transform) ||
               std::holds_alternative<ScaleGravity>(transform);
    case CompositionPolicy::ReplaceByPriority:
        return true;
    case CompositionPolicy::ConstraintResolved:
        return false;
    }
    return false;
}

void GravityOperatorGraph::apply_transform(fields::GravityVector<2>& value, const GravityTransform& transform)
{
    std::visit(
        [&value](const auto& operation) {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, RotateGravityQuarterTurns>) {
                auto turns = static_cast<int>(operation.counterclockwise_quarter_turns) % 4;
                if (turns < 0) {
                    turns += 4;
                }
                for (int turn = 0; turn < turns; ++turn) {
                    value = math::rotate_quarter_turn_counterclockwise(value);
                }
            } else if constexpr (std::same_as<Operation, ScaleGravity>) {
                value = value * operation.dimensionless_factor;
            } else if constexpr (std::same_as<Operation, OffsetGravity>) {
                value = value + operation.offset;
            }
        },
        transform);
}

fields::GravityVector<2> GravityOperatorGraph::apply(
    fields::GravityVector<2> base,
    const spacetime::Event<2>& event) const
{
    if (policy_ == CompositionPolicy::ReplaceByPriority) {
        const GravityOperatorNode* selected = nullptr;
        for (const auto& node : nodes_) {
            if (node.support.contains(event.position)) {
                selected = &node;
            }
        }
        if (selected != nullptr) {
            apply_transform(base, selected->transform);
        }
        return base;
    }

    for (const auto& node : nodes_) {
        if (node.support.contains(event.position)) {
            apply_transform(base, node.transform);
        }
    }
    return base;
}

OperatedGravityField2::OperatedGravityField2(
    std::unique_ptr<const fields::EffectiveGravityField<2>> base,
    GravityOperatorGraph operators)
    : base_(std::move(base)),
      operators_(std::move(operators)),
      metadata_(fields::effective_gravity_metadata<2>())
{
    if (!base_) {
        throw std::invalid_argument("an operated gravity field requires a base field");
    }
    if (!fields::valid_effective_gravity_metadata<2>(base_->metadata())) {
        throw std::invalid_argument("an operated gravity field requires correctly typed gravity metadata");
    }
    // The current scalar revision contract can represent the owned operator
    // graph exactly only when its base definition is immutable. A future
    // composite source stamp may lift this restriction without hashing two
    // independent 64-bit revisions into one collision-prone value.
    if (base_->revision() != 0U) {
        throw std::invalid_argument("an operated gravity field currently requires an immutable base field");
    }
    metadata_.name = "OperatedEffectiveNewtonianGravityField";
    metadata_.storage = fields::FieldStorageKind::Composite;
}

fields::GravityVector<2> OperatedGravityField2::sample(const spacetime::Event<2>& event) const
{
    return operators_.apply(base_->sample(event), event);
}

bool OperatedGravityField2::install_operator(GravityOperatorNode node)
{
    return operators_.add(std::move(node));
}

bool OperatedGravityField2::remove_operator(OperatorId id)
{
    return operators_.remove(id);
}

}  // namespace principia::operators
