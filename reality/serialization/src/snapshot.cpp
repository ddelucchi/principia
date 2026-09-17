#include <principia/serialization/snapshot.hpp>

#include <principia/units/quantity.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace principia::serialization {
namespace {

constexpr std::string_view snapshot_magic = "principia.snapshot";

[[nodiscard]] SnapshotError make_error(
    SnapshotErrorCode code,
    std::string message,
    std::size_t line = 0,
    std::size_t column = 0)
{
    return SnapshotError{code, line, column, std::move(message)};
}

[[nodiscard]] constexpr bool is_ascii_whitespace(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

struct Token {
    std::string_view text;
    std::size_t line{};
    std::size_t column{};
};

class TokenReader {
public:
    explicit TokenReader(std::string_view input) : input_(input) {}

    [[nodiscard]] std::expected<Token, SnapshotError> take(std::string_view description)
    {
        skip_whitespace();
        if (position_ == input_.size()) {
            return std::unexpected(make_error(
                SnapshotErrorCode::UnexpectedEnd,
                "expected " + std::string(description) + " before end of snapshot",
                line_,
                column_));
        }

        const auto token_start = position_;
        const auto token_line = line_;
        const auto token_column = column_;
        while (position_ < input_.size() && !is_ascii_whitespace(input_[position_])) {
            ++position_;
            ++column_;
        }
        return Token{input_.substr(token_start, position_ - token_start), token_line, token_column};
    }

    [[nodiscard]] std::expected<void, SnapshotError> expect(std::string_view expected)
    {
        auto token = take("'" + std::string(expected) + "'");
        if (!token) {
            return std::unexpected(token.error());
        }
        if (token->text != expected) {
            return std::unexpected(make_error(
                SnapshotErrorCode::UnexpectedToken,
                "expected '" + std::string(expected) + "', found '" + std::string(token->text) + "'",
                token->line,
                token->column));
        }
        return {};
    }

    [[nodiscard]] bool has_more()
    {
        skip_whitespace();
        return position_ != input_.size();
    }

private:
    void skip_whitespace()
    {
        while (position_ < input_.size() && is_ascii_whitespace(input_[position_])) {
            if (input_[position_] == '\n') {
                ++line_;
                column_ = 1;
            } else {
                ++column_;
            }
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

template <std::integral Integer>
[[nodiscard]] std::expected<Integer, SnapshotError> parse_integer(Token token, std::string_view description)
{
    Integer value{};
    const auto* const begin = token.text.data();
    const auto* const end = begin + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidInteger,
            "invalid " + std::string(description) + " integer '" + std::string(token.text) + "'",
            token.line,
            token.column));
    }
    return value;
}

template <std::integral Integer>
[[nodiscard]] std::expected<Integer, SnapshotError> read_integer(
    TokenReader& reader,
    std::string_view description)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    return parse_integer<Integer>(*token, description);
}

[[nodiscard]] std::expected<double, SnapshotError> parse_floating_point(
    Token token,
    std::string_view description)
{
    double value{};
    const auto* const begin = token.text.data();
    const auto* const end = begin + token.text.size();
    const auto result = std::from_chars(begin, end, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidFloatingPoint,
            "invalid " + std::string(description) + " number '" + std::string(token.text) + "'",
            token.line,
            token.column));
    }
    if (!std::isfinite(value)) {
        return std::unexpected(make_error(
            SnapshotErrorCode::NonFiniteValue,
            std::string(description) + " must be finite",
            token.line,
            token.column));
    }
    return value;
}

[[nodiscard]] std::expected<double, SnapshotError> read_floating_point(
    TokenReader& reader,
    std::string_view description)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    return parse_floating_point(*token, description);
}

[[nodiscard]] std::expected<std::size_t, SnapshotError> read_count(
    TokenReader& reader,
    std::string_view description,
    std::size_t maximum,
    SnapshotErrorCode limit_error)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    auto value = parse_integer<std::uint64_t>(*token, description);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidCount,
            std::string(description) + " exceeds the platform record limit",
            token->line,
            token->column));
    }
    if (*value > static_cast<std::uint64_t>(maximum)) {
        return std::unexpected(make_error(
            limit_error,
            std::string(description) + " exceeds the caller-supplied decode limit",
            token->line,
            token->column));
    }
    return static_cast<std::size_t>(*value);
}

[[nodiscard]] std::expected<ParticleConstraintV1, SnapshotError> read_constraint(TokenReader& reader)
{
    auto token = reader.take("particle constraint");
    if (!token) {
        return std::unexpected(token.error());
    }
    if (token->text == "free") {
        return ParticleConstraintV1::Free;
    }
    if (token->text == "fixed") {
        return ParticleConstraintV1::Fixed;
    }
    return std::unexpected(make_error(
        SnapshotErrorCode::InvalidEnum,
        "unknown particle constraint '" + std::string(token->text) + "'",
        token->line,
        token->column));
}

[[nodiscard]] std::expected<GravityCompositionPolicyV1, SnapshotError> read_composition_policy(
    TokenReader& reader)
{
    auto token = reader.take("gravity composition policy");
    if (!token) {
        return std::unexpected(token.error());
    }
    if (token->text == "additive") {
        return GravityCompositionPolicyV1::Additive;
    }
    if (token->text == "multiplicative") {
        return GravityCompositionPolicyV1::Multiplicative;
    }
    if (token->text == "replace_by_priority") {
        return GravityCompositionPolicyV1::ReplaceByPriority;
    }
    if (token->text == "transform_chain") {
        return GravityCompositionPolicyV1::TransformChain;
    }
    if (token->text == "constraint_resolved") {
        return GravityCompositionPolicyV1::ConstraintResolved;
    }
    return std::unexpected(make_error(
        SnapshotErrorCode::InvalidEnum,
        "unknown gravity composition policy '" + std::string(token->text) + "'",
        token->line,
        token->column));
}

[[nodiscard]] constexpr bool valid_constraint(ParticleConstraintV1 constraint) noexcept
{
    switch (constraint) {
    case ParticleConstraintV1::Free:
    case ParticleConstraintV1::Fixed:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid_composition_policy(GravityCompositionPolicyV1 policy) noexcept
{
    switch (policy) {
    case GravityCompositionPolicyV1::Additive:
    case GravityCompositionPolicyV1::Multiplicative:
    case GravityCompositionPolicyV1::ReplaceByPriority:
    case GravityCompositionPolicyV1::TransformChain:
    case GravityCompositionPolicyV1::ConstraintResolved:
        return true;
    }
    return false;
}

[[nodiscard]] bool transform_allowed(
    GravityCompositionPolicyV1 policy,
    const GravityTransformV1& transform) noexcept
{
    switch (policy) {
    case GravityCompositionPolicyV1::Additive:
        return std::holds_alternative<OffsetGravityV1>(transform);
    case GravityCompositionPolicyV1::Multiplicative:
        return std::holds_alternative<ScaleGravityV1>(transform);
    case GravityCompositionPolicyV1::ReplaceByPriority:
        return true;
    case GravityCompositionPolicyV1::TransformChain:
        return std::holds_alternative<RotateGravityV1>(transform) ||
               std::holds_alternative<ScaleGravityV1>(transform);
    case GravityCompositionPolicyV1::ConstraintResolved:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr double canonical_zero(double value) noexcept
{
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] constexpr std::int8_t canonical_quarter_turns(std::int8_t value) noexcept
{
    auto turns = static_cast<int>(value) % 4;
    if (turns < 0) {
        turns += 4;
    }
    return static_cast<std::int8_t>(turns);
}

void canonicalize(SnapshotV1& snapshot)
{
    snapshot.simulation_time_seconds = canonical_zero(snapshot.simulation_time_seconds);
    for (auto& particle : snapshot.particles) {
        particle.position_x_metres = canonical_zero(particle.position_x_metres);
        particle.position_y_metres = canonical_zero(particle.position_y_metres);
        particle.momentum_x_kilogram_metres_per_second =
            canonical_zero(particle.momentum_x_kilogram_metres_per_second);
        particle.momentum_y_kilogram_metres_per_second =
            canonical_zero(particle.momentum_y_kilogram_metres_per_second);
        particle.rest_mass_kilograms = canonical_zero(particle.rest_mass_kilograms);
    }
    std::ranges::sort(snapshot.particles, {}, &ParticleRecordV1::id);

    for (auto& gravity_operator : snapshot.gravity_operators) {
        gravity_operator.support_center_x_metres = canonical_zero(gravity_operator.support_center_x_metres);
        gravity_operator.support_center_y_metres = canonical_zero(gravity_operator.support_center_y_metres);
        gravity_operator.support_radius_metres = canonical_zero(gravity_operator.support_radius_metres);
        std::visit(
            [](auto& transform) {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV1>) {
                    transform.counterclockwise_quarter_turns =
                        canonical_quarter_turns(transform.counterclockwise_quarter_turns);
                } else if constexpr (std::same_as<Transform, ScaleGravityV1>) {
                    transform.dimensionless_factor = canonical_zero(transform.dimensionless_factor);
                } else if constexpr (std::same_as<Transform, OffsetGravityV1>) {
                    transform.x_metres_per_second_squared =
                        canonical_zero(transform.x_metres_per_second_squared);
                    transform.y_metres_per_second_squared =
                        canonical_zero(transform.y_metres_per_second_squared);
                }
            },
            gravity_operator.transform);
    }
    std::ranges::sort(snapshot.gravity_operators, [](const auto& left, const auto& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        return left.id < right.id;
    });
}

template <std::integral Integer>
[[nodiscard]] bool append_integer(std::string& output, Integer value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 10);
    if (result.ec != std::errc{}) {
        return false;
    }
    output.append(buffer.data(), result.ptr);
    return true;
}

[[nodiscard]] bool append_floating_point(std::string& output, double value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        canonical_zero(value),
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        return false;
    }
    output.append(buffer.data(), result.ptr);
    return true;
}

[[nodiscard]] constexpr std::string_view constraint_name(ParticleConstraintV1 constraint) noexcept
{
    switch (constraint) {
    case ParticleConstraintV1::Free:
        return "free";
    case ParticleConstraintV1::Fixed:
        return "fixed";
    }
    return {};
}

[[nodiscard]] constexpr std::string_view composition_policy_name(GravityCompositionPolicyV1 policy) noexcept
{
    switch (policy) {
    case GravityCompositionPolicyV1::Additive:
        return "additive";
    case GravityCompositionPolicyV1::Multiplicative:
        return "multiplicative";
    case GravityCompositionPolicyV1::ReplaceByPriority:
        return "replace_by_priority";
    case GravityCompositionPolicyV1::TransformChain:
        return "transform_chain";
    case GravityCompositionPolicyV1::ConstraintResolved:
        return "constraint_resolved";
    }
    return {};
}

[[nodiscard]] std::expected<ParticleConstraintV1, SnapshotError> capture_constraint(
    world::KinematicConstraint constraint)
{
    switch (constraint) {
    case world::KinematicConstraint::Free:
        return ParticleConstraintV1::Free;
    case world::KinematicConstraint::Fixed:
        return ParticleConstraintV1::Fixed;
    }
    return std::unexpected(make_error(SnapshotErrorCode::InvalidEnum, "unknown runtime particle constraint"));
}

[[nodiscard]] std::expected<GravityCompositionPolicyV1, SnapshotError> capture_composition_policy(
    operators::CompositionPolicy policy)
{
    switch (policy) {
    case operators::CompositionPolicy::Additive:
        return GravityCompositionPolicyV1::Additive;
    case operators::CompositionPolicy::Multiplicative:
        return GravityCompositionPolicyV1::Multiplicative;
    case operators::CompositionPolicy::ReplaceByPriority:
        return GravityCompositionPolicyV1::ReplaceByPriority;
    case operators::CompositionPolicy::TransformChain:
        return GravityCompositionPolicyV1::TransformChain;
    case operators::CompositionPolicy::ConstraintResolved:
        return GravityCompositionPolicyV1::ConstraintResolved;
    }
    return std::unexpected(make_error(SnapshotErrorCode::InvalidEnum, "unknown runtime gravity policy"));
}

[[nodiscard]] GravityTransformV1 capture_transform(const operators::GravityTransform& transform)
{
    return std::visit(
        [](const auto& operation) -> GravityTransformV1 {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, operators::RotateGravityQuarterTurns>) {
                return RotateGravityV1{operation.counterclockwise_quarter_turns};
            } else if constexpr (std::same_as<Operation, operators::ScaleGravity>) {
                return ScaleGravityV1{operation.dimensionless_factor};
            } else {
                return OffsetGravityV1{
                    units::in_metres_per_second_squared(operation.offset[0]),
                    units::in_metres_per_second_squared(operation.offset[1])};
            }
        },
        transform);
}

}  // namespace

std::expected<void, SnapshotError> validate_snapshot_v1(const SnapshotV1& snapshot)
{
    if (snapshot.schema_version != snapshot_schema_version_v1) {
        return std::unexpected(make_error(
            SnapshotErrorCode::UnsupportedSchemaVersion,
            "SnapshotV1 requires schema version 1"));
    }
    if (!std::isfinite(snapshot.simulation_time_seconds)) {
        return std::unexpected(make_error(
            SnapshotErrorCode::NonFiniteValue,
            "simulation time must be finite"));
    }
    if (snapshot.simulation_time_seconds < 0.0) {
        return std::unexpected(make_error(
            SnapshotErrorCode::NegativeSimulationTime,
            "simulation time must be nonnegative"));
    }
    if (snapshot.particle_revision == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(make_error(
            SnapshotErrorCode::RevisionExhausted,
            "particle revision must leave room for a subsequent mutation"));
    }
    if (snapshot.particles.size() > snapshot.particle_revision) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidRevision,
            "particle revision cannot be smaller than the number of live particles"));
    }
    if (!valid_composition_policy(snapshot.gravity_composition_policy)) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidEnum,
            "snapshot contains an unknown gravity composition policy"));
    }

    std::vector<std::uint64_t> particle_ids;
    particle_ids.reserve(snapshot.particles.size());
    for (const auto& particle : snapshot.particles) {
        if (particle.id == 0 || particle.material_id == 0) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidIdentifier,
                "particle and material IDs must be nonzero"));
        }
        particle_ids.push_back(particle.id);
        if (!valid_constraint(particle.constraint)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidEnum,
                "snapshot contains an unknown particle constraint"));
        }
        if (!std::isfinite(particle.position_x_metres) || !std::isfinite(particle.position_y_metres) ||
            !std::isfinite(particle.momentum_x_kilogram_metres_per_second) ||
            !std::isfinite(particle.momentum_y_kilogram_metres_per_second) ||
            !std::isfinite(particle.rest_mass_kilograms)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::NonFiniteValue,
                "particle SI scalar values must be finite"));
        }
        if (particle.rest_mass_kilograms <= 0.0) {
            return std::unexpected(make_error(
                SnapshotErrorCode::NonPositiveMass,
                "particle rest mass must be positive"));
        }
        if (particle.constraint == ParticleConstraintV1::Fixed &&
            (particle.momentum_x_kilogram_metres_per_second != 0.0 ||
             particle.momentum_y_kilogram_metres_per_second != 0.0)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::FixedParticleHasMomentum,
                "fixed particles must have zero canonical momentum"));
        }
    }
    std::ranges::sort(particle_ids);
    if (std::ranges::adjacent_find(particle_ids) != particle_ids.end()) {
        return std::unexpected(make_error(
            SnapshotErrorCode::DuplicateIdentifier,
            "particle IDs must be unique"));
    }

    std::vector<std::uint64_t> operator_ids;
    operator_ids.reserve(snapshot.gravity_operators.size());
    for (const auto& gravity_operator : snapshot.gravity_operators) {
        if (gravity_operator.id == 0) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidIdentifier,
                "gravity operator IDs must be nonzero"));
        }
        operator_ids.push_back(gravity_operator.id);
        if (!std::isfinite(gravity_operator.support_center_x_metres) ||
            !std::isfinite(gravity_operator.support_center_y_metres) ||
            !std::isfinite(gravity_operator.support_radius_metres)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::NonFiniteValue,
                "gravity support SI scalar values must be finite"));
        }
        if (gravity_operator.support_radius_metres < 0.0) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidRadius,
                "gravity support radius must be nonnegative"));
        }
        const bool finite_transform = std::visit(
            [](const auto& transform) {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV1>) {
                    return true;
                } else if constexpr (std::same_as<Transform, ScaleGravityV1>) {
                    return std::isfinite(transform.dimensionless_factor);
                } else {
                    return std::isfinite(transform.x_metres_per_second_squared) &&
                           std::isfinite(transform.y_metres_per_second_squared);
                }
            },
            gravity_operator.transform);
        if (!finite_transform) {
            return std::unexpected(make_error(
                SnapshotErrorCode::NonFiniteValue,
                "gravity transform scalar values must be finite"));
        }
        if (!transform_allowed(snapshot.gravity_composition_policy, gravity_operator.transform)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::IncompatibleGravityTransform,
                "gravity transform is incompatible with the graph composition policy"));
        }
    }
    std::ranges::sort(operator_ids);
    if (std::ranges::adjacent_find(operator_ids) != operator_ids.end()) {
        return std::unexpected(make_error(
            SnapshotErrorCode::DuplicateIdentifier,
            "gravity operator IDs must be unique"));
    }
    return {};
}

std::expected<SnapshotV1, SnapshotError> capture_snapshot_v1(
    const world::WorldState2& world,
    const operators::GravityOperatorGraph& gravity_operators)
{
    SnapshotV1 snapshot;
    snapshot.world_tick = world.tick;
    snapshot.simulation_time_seconds = units::in_seconds(world.simulation_time.elapsed());
    snapshot.particle_revision = world.particles.revision();
    snapshot.particles.reserve(world.particles.ordered_particles().size());

    for (const auto& [id, particle] : world.particles.ordered_particles()) {
        if (id != particle.id) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidIdentifier,
                "particle store key does not match the particle's persistent ID"));
        }
        auto constraint = capture_constraint(particle.constraint);
        if (!constraint) {
            return std::unexpected(constraint.error());
        }
        snapshot.particles.push_back(ParticleRecordV1{
            id.value(),
            units::in_metres(particle.position[0]),
            units::in_metres(particle.position[1]),
            units::in_kilogram_metres_per_second(particle.momentum[0]),
            units::in_kilogram_metres_per_second(particle.momentum[1]),
            units::in_kilograms(particle.rest_mass),
            particle.material.value(),
            *constraint});
    }

    auto composition_policy = capture_composition_policy(gravity_operators.composition_policy());
    if (!composition_policy) {
        return std::unexpected(composition_policy.error());
    }
    snapshot.gravity_composition_policy = *composition_policy;
    snapshot.gravity_operators.reserve(gravity_operators.ordered_nodes().size());
    for (const auto& node : gravity_operators.ordered_nodes()) {
        snapshot.gravity_operators.push_back(GravityOperatorRecordV1{
            node.id.value(),
            node.priority,
            units::in_metres(node.support.center[0]),
            units::in_metres(node.support.center[1]),
            units::in_metres(node.support.radius),
            capture_transform(node.transform)});
    }

    canonicalize(snapshot);
    auto validity = validate_snapshot_v1(snapshot);
    if (!validity) {
        return std::unexpected(validity.error());
    }
    return snapshot;
}

std::expected<std::string, SnapshotError> encode_snapshot_v1(const SnapshotV1& snapshot)
{
    auto canonical = snapshot;
    canonicalize(canonical);
    auto validity = validate_snapshot_v1(canonical);
    if (!validity) {
        return std::unexpected(validity.error());
    }

    std::string output;
    output.reserve(256);
    output += snapshot_magic;
    output.push_back(' ');
    if (!append_integer(output, canonical.schema_version)) {
        return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode schema version"));
    }
    output += "\nworld ";
    if (!append_integer(output, canonical.world_tick)) {
        return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode world tick"));
    }
    output.push_back(' ');
    if (!append_floating_point(output, canonical.simulation_time_seconds)) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidFloatingPoint,
            "could not encode simulation time"));
    }
    output.push_back(' ');
    if (!append_integer(output, canonical.particle_revision)) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidInteger,
            "could not encode particle revision"));
    }

    output += "\nparticles ";
    if (!append_integer(output, canonical.particles.size())) {
        return std::unexpected(make_error(SnapshotErrorCode::InvalidCount, "could not encode particle count"));
    }
    output.push_back('\n');
    for (const auto& particle : canonical.particles) {
        output += "particle ";
        if (!append_integer(output, particle.id)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode particle ID"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, particle.position_x_metres)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode particle position"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, particle.position_y_metres)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode particle position"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, particle.momentum_x_kilogram_metres_per_second)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode particle momentum"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, particle.momentum_y_kilogram_metres_per_second)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode particle momentum"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, particle.rest_mass_kilograms)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode particle mass"));
        }
        output.push_back(' ');
        if (!append_integer(output, particle.material_id)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode material ID"));
        }
        output.push_back(' ');
        output += constraint_name(particle.constraint);
        output.push_back('\n');
    }

    output += "gravity ";
    output += composition_policy_name(canonical.gravity_composition_policy);
    output.push_back(' ');
    if (!append_integer(output, canonical.gravity_operators.size())) {
        return std::unexpected(make_error(SnapshotErrorCode::InvalidCount, "could not encode gravity operator count"));
    }
    output.push_back('\n');
    for (const auto& gravity_operator : canonical.gravity_operators) {
        output += "operator ";
        if (!append_integer(output, gravity_operator.id)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode operator ID"));
        }
        output.push_back(' ');
        if (!append_integer(output, gravity_operator.priority)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidInteger, "could not encode operator priority"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, gravity_operator.support_center_x_metres)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode operator support"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, gravity_operator.support_center_y_metres)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode operator support"));
        }
        output.push_back(' ');
        if (!append_floating_point(output, gravity_operator.support_radius_metres)) {
            return std::unexpected(make_error(SnapshotErrorCode::InvalidFloatingPoint, "could not encode operator support"));
        }
        const bool transform_encoded = std::visit(
            [&output](const auto& transform) -> bool {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV1>) {
                    output += " rotate_quarter_turns ";
                    return append_integer(output, static_cast<int>(transform.counterclockwise_quarter_turns));
                } else if constexpr (std::same_as<Transform, ScaleGravityV1>) {
                    output += " scale ";
                    return append_floating_point(output, transform.dimensionless_factor);
                } else {
                    output += " offset ";
                    if (!append_floating_point(output, transform.x_metres_per_second_squared)) {
                        return false;
                    }
                    output.push_back(' ');
                    return append_floating_point(output, transform.y_metres_per_second_squared);
                }
            },
            gravity_operator.transform);
        if (!transform_encoded) {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidFloatingPoint,
                "could not encode gravity transform"));
        }
        output.push_back('\n');
    }
    output += "end\n";
    return output;
}

std::expected<SnapshotV1, SnapshotError> decode_snapshot_v1(
    std::string_view encoded,
    SnapshotDecodeLimits limits)
{
    if (encoded.size() > limits.maximum_input_bytes) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InputLimitExceeded,
            "snapshot input exceeds the caller-supplied byte limit"));
    }
    TokenReader reader(encoded);
    auto magic = reader.take("snapshot header");
    if (!magic) {
        return std::unexpected(magic.error());
    }
    if (magic->text != snapshot_magic) {
        return std::unexpected(make_error(
            SnapshotErrorCode::InvalidHeader,
            "invalid snapshot header '" + std::string(magic->text) + "'",
            magic->line,
            magic->column));
    }

    auto version = read_integer<std::uint32_t>(reader, "schema version");
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version != snapshot_schema_version_v1) {
        return std::unexpected(make_error(
            SnapshotErrorCode::UnsupportedSchemaVersion,
            "unsupported snapshot schema version " + std::to_string(*version)));
    }

    if (auto marker = reader.expect("world"); !marker) {
        return std::unexpected(marker.error());
    }
    auto world_tick = read_integer<std::uint64_t>(reader, "world tick");
    if (!world_tick) {
        return std::unexpected(world_tick.error());
    }
    auto simulation_time = read_floating_point(reader, "simulation time");
    if (!simulation_time) {
        return std::unexpected(simulation_time.error());
    }
    auto particle_revision = read_integer<std::uint64_t>(reader, "particle revision");
    if (!particle_revision) {
        return std::unexpected(particle_revision.error());
    }

    if (auto marker = reader.expect("particles"); !marker) {
        return std::unexpected(marker.error());
    }
    auto particle_count = read_count(
        reader,
        "particle count",
        limits.maximum_particles,
        SnapshotErrorCode::ParticleLimitExceeded);
    if (!particle_count) {
        return std::unexpected(particle_count.error());
    }

    SnapshotV1 snapshot;
    snapshot.world_tick = *world_tick;
    snapshot.simulation_time_seconds = *simulation_time;
    snapshot.particle_revision = *particle_revision;
    for (std::size_t index = 0; index < *particle_count; ++index) {
        static_cast<void>(index);
        if (auto marker = reader.expect("particle"); !marker) {
            return std::unexpected(marker.error());
        }
        auto id = read_integer<std::uint64_t>(reader, "particle ID");
        if (!id) {
            return std::unexpected(id.error());
        }
        auto position_x = read_floating_point(reader, "particle x position");
        if (!position_x) {
            return std::unexpected(position_x.error());
        }
        auto position_y = read_floating_point(reader, "particle y position");
        if (!position_y) {
            return std::unexpected(position_y.error());
        }
        auto momentum_x = read_floating_point(reader, "particle x momentum");
        if (!momentum_x) {
            return std::unexpected(momentum_x.error());
        }
        auto momentum_y = read_floating_point(reader, "particle y momentum");
        if (!momentum_y) {
            return std::unexpected(momentum_y.error());
        }
        auto mass = read_floating_point(reader, "particle rest mass");
        if (!mass) {
            return std::unexpected(mass.error());
        }
        auto material_id = read_integer<std::uint32_t>(reader, "material ID");
        if (!material_id) {
            return std::unexpected(material_id.error());
        }
        auto constraint = read_constraint(reader);
        if (!constraint) {
            return std::unexpected(constraint.error());
        }
        snapshot.particles.push_back(ParticleRecordV1{
            *id,
            *position_x,
            *position_y,
            *momentum_x,
            *momentum_y,
            *mass,
            *material_id,
            *constraint});
    }

    if (auto marker = reader.expect("gravity"); !marker) {
        return std::unexpected(marker.error());
    }
    auto composition_policy = read_composition_policy(reader);
    if (!composition_policy) {
        return std::unexpected(composition_policy.error());
    }
    snapshot.gravity_composition_policy = *composition_policy;
    auto operator_count = read_count(
        reader,
        "gravity operator count",
        limits.maximum_gravity_operators,
        SnapshotErrorCode::GravityOperatorLimitExceeded);
    if (!operator_count) {
        return std::unexpected(operator_count.error());
    }

    for (std::size_t index = 0; index < *operator_count; ++index) {
        static_cast<void>(index);
        if (auto marker = reader.expect("operator"); !marker) {
            return std::unexpected(marker.error());
        }
        auto id = read_integer<std::uint64_t>(reader, "gravity operator ID");
        if (!id) {
            return std::unexpected(id.error());
        }
        auto priority = read_integer<std::int32_t>(reader, "gravity operator priority");
        if (!priority) {
            return std::unexpected(priority.error());
        }
        auto center_x = read_floating_point(reader, "gravity support x center");
        if (!center_x) {
            return std::unexpected(center_x.error());
        }
        auto center_y = read_floating_point(reader, "gravity support y center");
        if (!center_y) {
            return std::unexpected(center_y.error());
        }
        auto radius = read_floating_point(reader, "gravity support radius");
        if (!radius) {
            return std::unexpected(radius.error());
        }

        auto transform_token = reader.take("gravity transform kind");
        if (!transform_token) {
            return std::unexpected(transform_token.error());
        }
        GravityTransformV1 transform;
        if (transform_token->text == "rotate_quarter_turns") {
            auto turns = read_integer<int>(reader, "counterclockwise quarter turns");
            if (!turns) {
                return std::unexpected(turns.error());
            }
            constexpr auto minimum_turns = static_cast<int>(std::numeric_limits<std::int8_t>::min());
            constexpr auto maximum_turns = static_cast<int>(std::numeric_limits<std::int8_t>::max());
            if (*turns < minimum_turns || *turns > maximum_turns) {
                return std::unexpected(make_error(
                    SnapshotErrorCode::InvalidInteger,
                    "counterclockwise quarter turns must fit in a signed byte",
                    transform_token->line,
                    transform_token->column));
            }
            transform = RotateGravityV1{static_cast<std::int8_t>(*turns)};
        } else if (transform_token->text == "scale") {
            auto factor = read_floating_point(reader, "gravity scale factor");
            if (!factor) {
                return std::unexpected(factor.error());
            }
            transform = ScaleGravityV1{*factor};
        } else if (transform_token->text == "offset") {
            auto offset_x = read_floating_point(reader, "gravity x offset");
            if (!offset_x) {
                return std::unexpected(offset_x.error());
            }
            auto offset_y = read_floating_point(reader, "gravity y offset");
            if (!offset_y) {
                return std::unexpected(offset_y.error());
            }
            transform = OffsetGravityV1{*offset_x, *offset_y};
        } else {
            return std::unexpected(make_error(
                SnapshotErrorCode::InvalidEnum,
                "unknown gravity transform kind '" + std::string(transform_token->text) + "'",
                transform_token->line,
                transform_token->column));
        }

        if (!transform_allowed(snapshot.gravity_composition_policy, transform)) {
            return std::unexpected(make_error(
                SnapshotErrorCode::IncompatibleGravityTransform,
                "gravity transform is incompatible with the graph composition policy",
                transform_token->line,
                transform_token->column));
        }
        snapshot.gravity_operators.push_back(GravityOperatorRecordV1{
            *id,
            *priority,
            *center_x,
            *center_y,
            *radius,
            std::move(transform)});
    }

    if (auto marker = reader.expect("end"); !marker) {
        return std::unexpected(marker.error());
    }
    if (reader.has_more()) {
        auto trailing = reader.take("end of snapshot");
        if (!trailing) {
            return std::unexpected(trailing.error());
        }
        return std::unexpected(make_error(
            SnapshotErrorCode::TrailingData,
            "unexpected data after snapshot end marker",
            trailing->line,
            trailing->column));
    }

    canonicalize(snapshot);
    auto validity = validate_snapshot_v1(snapshot);
    if (!validity) {
        return std::unexpected(validity.error());
    }
    return snapshot;
}

}  // namespace principia::serialization
