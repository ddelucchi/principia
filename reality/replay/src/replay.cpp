#include <principia/replay/replay.hpp>

#include <principia/fields/field.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace principia::replay {
namespace {

constexpr std::string_view replay_magic = "principia.replay";

[[nodiscard]] ReplayError make_error(
    ReplayErrorCode code,
    std::string message,
    std::size_t line = 0,
    std::size_t column = 0,
    CommandKeyV1 key = {})
{
    return ReplayError{code, line, column, key.tick, key.sequence, std::move(message), std::nullopt};
}

[[nodiscard]] ReplayError snapshot_error(
    ReplayErrorCode code,
    std::string_view context,
    const serialization::SnapshotError& error)
{
    auto result = make_error(
        code,
        std::string(context) + ": " + error.message,
        error.line,
        error.column);
    result.snapshot_cause = error.code;
    return result;
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

[[nodiscard]] bool command_less(const CommandV1& left, const CommandV1& right) noexcept
{
    return left.key < right.key;
}

void canonicalize_operator(serialization::GravityOperatorRecordV1& gravity_operator)
{
    gravity_operator.support_center_x_metres = canonical_zero(gravity_operator.support_center_x_metres);
    gravity_operator.support_center_y_metres = canonical_zero(gravity_operator.support_center_y_metres);
    gravity_operator.support_radius_metres = canonical_zero(gravity_operator.support_radius_metres);
    std::visit(
        [](auto& transform) {
            using Transform = std::remove_cvref_t<decltype(transform)>;
            if constexpr (std::same_as<Transform, serialization::RotateGravityV1>) {
                transform.counterclockwise_quarter_turns =
                    canonical_quarter_turns(transform.counterclockwise_quarter_turns);
            } else if constexpr (std::same_as<Transform, serialization::ScaleGravityV1>) {
                transform.dimensionless_factor = canonical_zero(transform.dimensionless_factor);
            } else if constexpr (std::same_as<Transform, serialization::OffsetGravityV1>) {
                transform.x_metres_per_second_squared = canonical_zero(transform.x_metres_per_second_squared);
                transform.y_metres_per_second_squared = canonical_zero(transform.y_metres_per_second_squared);
            }
        },
        gravity_operator.transform);
}

void canonicalize(ReplayV1& replay)
{
    replay.base_gravity_x_metres_per_second_squared =
        canonical_zero(replay.base_gravity_x_metres_per_second_squared);
    replay.base_gravity_y_metres_per_second_squared =
        canonical_zero(replay.base_gravity_y_metres_per_second_squared);
    replay.fixed_dt_seconds = canonical_zero(replay.fixed_dt_seconds);
    for (auto& command : replay.commands) {
        std::visit(
            [](auto& payload) {
                using Payload = std::remove_cvref_t<decltype(payload)>;
                if constexpr (std::same_as<Payload, ApplyImpulseCommandV1>) {
                    payload.impulse_x_kilogram_metres_per_second =
                        canonical_zero(payload.impulse_x_kilogram_metres_per_second);
                    payload.impulse_y_kilogram_metres_per_second =
                        canonical_zero(payload.impulse_y_kilogram_metres_per_second);
                } else if constexpr (std::same_as<Payload, InstallGravityOperatorCommandV1>) {
                    canonicalize_operator(payload.gravity_operator);
                }
            },
            command.payload);
    }
    std::ranges::sort(replay.commands, command_less);
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

struct Token {
    std::string_view text;
    std::size_t line{};
    std::size_t column{};
};

[[nodiscard]] constexpr bool is_ascii_whitespace(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

class ReplayReader {
public:
    explicit ReplayReader(std::string_view input) : input_(input) {}

    [[nodiscard]] std::expected<Token, ReplayError> take(std::string_view description)
    {
        skip_whitespace();
        if (position_ == input_.size()) {
            return std::unexpected(make_error(
                ReplayErrorCode::UnexpectedEnd,
                "expected " + std::string(description) + " before end of replay",
                line_,
                column_));
        }
        const auto start = position_;
        const auto token_line = line_;
        const auto token_column = column_;
        while (position_ < input_.size() && !is_ascii_whitespace(input_[position_])) {
            advance_one();
        }
        return Token{input_.substr(start, position_ - start), token_line, token_column};
    }

    [[nodiscard]] std::expected<void, ReplayError> expect(std::string_view expected)
    {
        auto token = take("'" + std::string(expected) + "'");
        if (!token) {
            return std::unexpected(token.error());
        }
        if (token->text != expected) {
            return std::unexpected(make_error(
                ReplayErrorCode::UnexpectedToken,
                "expected '" + std::string(expected) + "', found '" + std::string(token->text) + "'",
                token->line,
                token->column));
        }
        return {};
    }

    [[nodiscard]] std::expected<std::string_view, ReplayError> take_payload(std::size_t size)
    {
        if (position_ < input_.size() && input_[position_] == '\r') {
            advance_one();
        }
        if (position_ == input_.size() || input_[position_] != '\n') {
            return std::unexpected(make_error(
                ReplayErrorCode::UnexpectedToken,
                "snapshot byte count must be followed by a line break",
                line_,
                column_));
        }
        advance_one();
        if (size > input_.size() - position_) {
            return std::unexpected(make_error(
                ReplayErrorCode::UnexpectedEnd,
                "snapshot payload is shorter than its declared byte count",
                line_,
                column_));
        }
        const auto start = position_;
        for (std::size_t index = 0; index < size; ++index) {
            advance_one();
        }
        return input_.substr(start, size);
    }

    [[nodiscard]] bool has_more()
    {
        skip_whitespace();
        return position_ != input_.size();
    }

private:
    void advance_one()
    {
        if (input_[position_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++position_;
    }

    void skip_whitespace()
    {
        while (position_ < input_.size() && is_ascii_whitespace(input_[position_])) {
            advance_one();
        }
    }

    std::string_view input_;
    std::size_t position_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

template <std::integral Integer>
[[nodiscard]] std::expected<Integer, ReplayError> parse_integer(Token token, std::string_view description)
{
    Integer value{};
    const auto* const begin = token.text.data();
    const auto* const end = begin + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::unexpected(make_error(
            ReplayErrorCode::InvalidInteger,
            "invalid " + std::string(description) + " integer '" + std::string(token.text) + "'",
            token.line,
            token.column));
    }
    return value;
}

template <std::integral Integer>
[[nodiscard]] std::expected<Integer, ReplayError> read_integer(
    ReplayReader& reader,
    std::string_view description)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    return parse_integer<Integer>(*token, description);
}

[[nodiscard]] std::expected<double, ReplayError> read_floating_point(
    ReplayReader& reader,
    std::string_view description)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    double value{};
    const auto* const begin = token->text.data();
    const auto* const end = begin + token->text.size();
    const auto result = std::from_chars(begin, end, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::unexpected(make_error(
            ReplayErrorCode::InvalidFloatingPoint,
            "invalid " + std::string(description) + " number '" + std::string(token->text) + "'",
            token->line,
            token->column));
    }
    if (!std::isfinite(value)) {
        return std::unexpected(make_error(
            ReplayErrorCode::NonFiniteValue,
            std::string(description) + " must be finite",
            token->line,
            token->column));
    }
    return value;
}

[[nodiscard]] std::expected<std::size_t, ReplayError> read_count(
    ReplayReader& reader,
    std::string_view description,
    std::size_t maximum,
    ReplayErrorCode limit_error)
{
    auto token = reader.take(description);
    if (!token) {
        return std::unexpected(token.error());
    }
    auto value = parse_integer<std::uint64_t>(*token, description);
    if (!value) {
        return std::unexpected(value.error());
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

[[nodiscard]] std::expected<operators::CompositionPolicy, ReplayError> runtime_policy(
    serialization::GravityCompositionPolicyV1 policy)
{
    switch (policy) {
    case serialization::GravityCompositionPolicyV1::Additive:
        return operators::CompositionPolicy::Additive;
    case serialization::GravityCompositionPolicyV1::Multiplicative:
        return operators::CompositionPolicy::Multiplicative;
    case serialization::GravityCompositionPolicyV1::ReplaceByPriority:
        return operators::CompositionPolicy::ReplaceByPriority;
    case serialization::GravityCompositionPolicyV1::TransformChain:
        return operators::CompositionPolicy::TransformChain;
    case serialization::GravityCompositionPolicyV1::ConstraintResolved:
        return operators::CompositionPolicy::ConstraintResolved;
    }
    return std::unexpected(make_error(ReplayErrorCode::InvalidSnapshot, "unknown gravity composition policy"));
}

[[nodiscard]] world::KinematicConstraint runtime_constraint(serialization::ParticleConstraintV1 constraint)
{
    switch (constraint) {
    case serialization::ParticleConstraintV1::Free:
        return world::KinematicConstraint::Free;
    case serialization::ParticleConstraintV1::Fixed:
        return world::KinematicConstraint::Fixed;
    }
    return world::KinematicConstraint::Free;
}

[[nodiscard]] operators::GravityTransform runtime_transform(const serialization::GravityTransformV1& transform)
{
    return std::visit(
        [](const auto& operation) -> operators::GravityTransform {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, serialization::RotateGravityV1>) {
                return operators::RotateGravityQuarterTurns{operation.counterclockwise_quarter_turns};
            } else if constexpr (std::same_as<Operation, serialization::ScaleGravityV1>) {
                return operators::ScaleGravity{operation.dimensionless_factor};
            } else {
                return operators::OffsetGravity{fields::GravityVector<2>{
                    units::metres_per_second_squared(operation.x_metres_per_second_squared),
                    units::metres_per_second_squared(operation.y_metres_per_second_squared)}};
            }
        },
        transform);
}

[[nodiscard]] operators::GravityOperatorNode runtime_operator(
    const serialization::GravityOperatorRecordV1& gravity_operator)
{
    return operators::GravityOperatorNode{
        operators::OperatorId{gravity_operator.id},
        gravity_operator.priority,
        operators::CircularRegion2{
            spacetime::WorldPosition<2>{
                units::metres(gravity_operator.support_center_x_metres),
                units::metres(gravity_operator.support_center_y_metres)},
            units::metres(gravity_operator.support_radius_metres)},
        runtime_transform(gravity_operator.transform)};
}

[[nodiscard]] std::expected<void, ReplayError> validate_operator_record(
    serialization::GravityCompositionPolicyV1 policy,
    const serialization::GravityOperatorRecordV1& gravity_operator,
    CommandKeyV1 key)
{
    serialization::SnapshotV1 probe;
    probe.gravity_composition_policy = policy;
    probe.gravity_operators.push_back(gravity_operator);
    auto validity = serialization::validate_snapshot_v1(probe);
    if (!validity) {
        auto error = snapshot_error(ReplayErrorCode::InvalidGravityOperator, "invalid installed gravity operator", validity.error());
        error.tick = key.tick;
        error.sequence = key.sequence;
        return std::unexpected(std::move(error));
    }
    return {};
}

[[nodiscard]] std::expected<ReplayRuntimeState2, ReplayError> restore_validated(const ReplayV1& replay)
{
    world::ParticleSnapshot2 particles;
    particles.revision = replay.initial_snapshot.particle_revision;
    particles.particles.reserve(replay.initial_snapshot.particles.size());
    for (const auto& particle : replay.initial_snapshot.particles) {
        particles.particles.push_back(world::ParticleState2{
            world::ParticleId{particle.id},
            spacetime::WorldPosition<2>{
                units::metres(particle.position_x_metres),
                units::metres(particle.position_y_metres)},
            math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(particle.momentum_x_kilogram_metres_per_second),
                units::kilogram_metres_per_second(particle.momentum_y_kilogram_metres_per_second)},
            units::kilograms(particle.rest_mass_kilograms),
            materials::MaterialId{particle.material_id},
            runtime_constraint(particle.constraint)});
    }

    world::WorldState2 world;
    auto restored = world.particles.restore(std::move(particles));
    if (!restored) {
        return std::unexpected(make_error(
            ReplayErrorCode::SnapshotRestoreFailed,
            "validated snapshot could not be restored into the particle store"));
    }
    world.tick = replay.initial_snapshot.world_tick;
    world.simulation_time = spacetime::SimulationTime{
        units::seconds(replay.initial_snapshot.simulation_time_seconds)};

    auto policy = runtime_policy(replay.initial_snapshot.gravity_composition_policy);
    if (!policy) {
        return std::unexpected(policy.error());
    }
    operators::GravityOperatorGraph graph{*policy};
    for (const auto& gravity_operator : replay.initial_snapshot.gravity_operators) {
        if (!graph.add(runtime_operator(gravity_operator))) {
            return std::unexpected(make_error(
                ReplayErrorCode::SnapshotRestoreFailed,
                "validated snapshot gravity graph could not be restored"));
        }
    }
    return ReplayRuntimeState2{std::move(world), std::move(graph)};
}

class ReplayGravityField2 final : public fields::EffectiveGravityField<2> {
public:
    ReplayGravityField2(fields::GravityVector<2> base, const operators::GravityOperatorGraph& operators)
        : base_(std::move(base)), operators_(operators), metadata_(fields::effective_gravity_metadata<2>())
    {
        metadata_.name = "ReplayEffectiveNewtonianGravityField";
        metadata_.storage = fields::FieldStorageKind::Composite;
    }

    [[nodiscard]] fields::GravityVector<2> sample(const spacetime::Event<2>& event) const override
    {
        return operators_.apply(base_, event);
    }

    [[nodiscard]] const fields::FieldMetadata& metadata() const noexcept override { return metadata_; }
    [[nodiscard]] std::uint64_t revision() const noexcept override { return operators_.revision(); }

private:
    fields::GravityVector<2> base_;
    const operators::GravityOperatorGraph& operators_;
    fields::FieldMetadata metadata_;
};

[[nodiscard]] std::expected<void, ReplayError> execute_command(
    ReplayRuntimeState2& state,
    const CommandV1& command)
{
    return std::visit(
        [&state, &command](const auto& payload) -> std::expected<void, ReplayError> {
            using Payload = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::same_as<Payload, ApplyImpulseCommandV1>) {
                const math::Vector<2, units::Momentum> impulse{
                    units::kilogram_metres_per_second(payload.impulse_x_kilogram_metres_per_second),
                    units::kilogram_metres_per_second(payload.impulse_y_kilogram_metres_per_second)};
                const auto applied = state.world.particles.apply_impulse(
                    world::ParticleId{payload.particle_id},
                    impulse);
                if (!applied) {
                    switch (applied.error()) {
                    case world::ParticleMutationError::MissingParticle:
                        return std::unexpected(make_error(
                            ReplayErrorCode::MissingParticle,
                            "impulse command references a missing particle",
                            0,
                            0,
                            command.key));
                    case world::ParticleMutationError::ConstrainedParticle:
                        return std::unexpected(make_error(
                            ReplayErrorCode::ConstrainedParticle,
                            "impulse command cannot mutate a constrained particle",
                            0,
                            0,
                            command.key));
                    case world::ParticleMutationError::NonFiniteState:
                        return std::unexpected(make_error(
                            ReplayErrorCode::CommandProducedNonFiniteState,
                            "impulse command produced non-finite momentum",
                            0,
                            0,
                            command.key));
                    case world::ParticleMutationError::RevisionExhausted:
                        return std::unexpected(make_error(
                            ReplayErrorCode::RevisionExhausted,
                            "particle revision is exhausted",
                            0,
                            0,
                            command.key));
                    default:
                        return std::unexpected(make_error(
                            ReplayErrorCode::ParticleMutationFailed,
                            "particle store rejected the impulse command",
                            0,
                            0,
                            command.key));
                    }
                }
            } else if constexpr (std::same_as<Payload, InstallGravityOperatorCommandV1>) {
                if (!state.gravity_operators.add(runtime_operator(payload.gravity_operator))) {
                    return std::unexpected(make_error(
                        ReplayErrorCode::DuplicateGravityOperator,
                        "gravity operator installation failed",
                        0,
                        0,
                        command.key));
                }
            } else {
                if (!state.gravity_operators.remove(operators::OperatorId{payload.operator_id})) {
                    return std::unexpected(make_error(
                        ReplayErrorCode::MissingGravityOperator,
                        "gravity operator removal references a missing operator",
                        0,
                        0,
                        command.key));
                }
            }
            return {};
        },
        command.payload);
}

}  // namespace

std::expected<void, ReplayError> validate_replay_v1(const ReplayV1& replay)
{
    if (replay.schema_version != replay_schema_version_v1) {
        return std::unexpected(make_error(
            ReplayErrorCode::UnsupportedSchemaVersion,
            "ReplayV1 requires schema version 1"));
    }
    auto snapshot_validity = serialization::validate_snapshot_v1(replay.initial_snapshot);
    if (!snapshot_validity) {
        return std::unexpected(snapshot_error(
            ReplayErrorCode::InvalidSnapshot,
            "invalid initial snapshot",
            snapshot_validity.error()));
    }
    if (!std::isfinite(replay.base_gravity_x_metres_per_second_squared) ||
        !std::isfinite(replay.base_gravity_y_metres_per_second_squared) ||
        !std::isfinite(replay.fixed_dt_seconds)) {
        return std::unexpected(make_error(
            ReplayErrorCode::NonFiniteValue,
            "base gravity and fixed time step must be finite"));
    }
    if (replay.fixed_dt_seconds <= 0.0) {
        return std::unexpected(make_error(
            ReplayErrorCode::NonPositiveTimeStep,
            "fixed time step must be positive"));
    }
    if (replay.end_tick < replay.initial_snapshot.world_tick) {
        return std::unexpected(make_error(
            ReplayErrorCode::EndTickBeforeInitialTick,
            "end tick cannot precede the initial snapshot tick"));
    }
    auto commands = replay.commands;
    std::ranges::sort(commands, command_less);
    std::map<std::uint64_t, serialization::ParticleConstraintV1> particle_constraints;
    for (const auto& particle : replay.initial_snapshot.particles) {
        particle_constraints.emplace(particle.id, particle.constraint);
    }
    std::set<std::uint64_t> operator_ids;
    for (const auto& gravity_operator : replay.initial_snapshot.gravity_operators) {
        operator_ids.insert(gravity_operator.id);
    }

    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];
        if (command.key.tick < replay.initial_snapshot.world_tick || command.key.tick >= replay.end_tick) {
            return std::unexpected(make_error(
                ReplayErrorCode::CommandOutsideTickRange,
                "command tick must be in [initial tick, end tick)",
                0,
                0,
                command.key));
        }
        if (index != 0 && commands[index - 1].key == command.key) {
            return std::unexpected(make_error(
                ReplayErrorCode::DuplicateCommandKey,
                "command tick/sequence keys must be unique",
                0,
                0,
                command.key));
        }

        auto command_validity = std::visit(
            [&replay, &particle_constraints, &operator_ids, &command](const auto& payload)
                -> std::expected<void, ReplayError> {
                using Payload = std::remove_cvref_t<decltype(payload)>;
                if constexpr (std::same_as<Payload, ApplyImpulseCommandV1>) {
                    if (payload.particle_id == 0) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::InvalidIdentifier,
                            "impulse particle ID must be nonzero",
                            0,
                            0,
                            command.key));
                    }
                    if (!std::isfinite(payload.impulse_x_kilogram_metres_per_second) ||
                        !std::isfinite(payload.impulse_y_kilogram_metres_per_second)) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::NonFiniteValue,
                            "impulse SI scalar values must be finite",
                            0,
                            0,
                            command.key));
                    }
                    const auto particle = particle_constraints.find(payload.particle_id);
                    if (particle == particle_constraints.end()) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::MissingParticle,
                            "impulse command references a missing particle",
                            0,
                            0,
                            command.key));
                    }
                    if (particle->second != serialization::ParticleConstraintV1::Free) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::ConstrainedParticle,
                            "impulse command cannot target a constrained particle",
                            0,
                            0,
                            command.key));
                    }
                } else if constexpr (std::same_as<Payload, InstallGravityOperatorCommandV1>) {
                    auto operator_validity = validate_operator_record(
                        replay.initial_snapshot.gravity_composition_policy,
                        payload.gravity_operator,
                        command.key);
                    if (!operator_validity) {
                        return std::unexpected(operator_validity.error());
                    }
                    if (!operator_ids.insert(payload.gravity_operator.id).second) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::DuplicateGravityOperator,
                            "gravity operator ID is already installed",
                            0,
                            0,
                            command.key));
                    }
                } else {
                    if (payload.operator_id == 0) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::InvalidIdentifier,
                            "removed gravity operator ID must be nonzero",
                            0,
                            0,
                            command.key));
                    }
                    if (operator_ids.erase(payload.operator_id) == 0) {
                        return std::unexpected(make_error(
                            ReplayErrorCode::MissingGravityOperator,
                            "gravity operator removal references a missing operator",
                            0,
                            0,
                            command.key));
                    }
                }
                return {};
            },
            command.payload);
        if (!command_validity) {
            return std::unexpected(command_validity.error());
        }
    }
    return {};
}

std::expected<std::string, ReplayError> encode_replay_v1(const ReplayV1& replay)
{
    auto validity = validate_replay_v1(replay);
    if (!validity) {
        return std::unexpected(validity.error());
    }
    auto canonical = replay;
    canonicalize(canonical);
    auto snapshot = serialization::encode_snapshot_v1(canonical.initial_snapshot);
    if (!snapshot) {
        return std::unexpected(snapshot_error(
            ReplayErrorCode::InvalidSnapshot,
            "initial snapshot could not be encoded",
            snapshot.error()));
    }

    std::string output;
    output.reserve(snapshot->size() + canonical.commands.size() * 96 + 128);
    output += replay_magic;
    output.push_back(' ');
    if (!append_integer(output, canonical.schema_version)) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidInteger, "could not encode schema version"));
    }
    output += "\nconfig ";
    if (!append_floating_point(output, canonical.base_gravity_x_metres_per_second_squared)) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidFloatingPoint, "could not encode base gravity"));
    }
    output.push_back(' ');
    if (!append_floating_point(output, canonical.base_gravity_y_metres_per_second_squared)) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidFloatingPoint, "could not encode base gravity"));
    }
    output.push_back(' ');
    if (!append_floating_point(output, canonical.fixed_dt_seconds)) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidFloatingPoint, "could not encode fixed time step"));
    }
    output.push_back(' ');
    if (!append_integer(output, canonical.end_tick)) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidInteger, "could not encode end tick"));
    }
    output += "\nsnapshot_bytes ";
    if (!append_integer(output, snapshot->size())) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidCount, "could not encode snapshot byte count"));
    }
    output.push_back('\n');
    output += *snapshot;
    output += "commands ";
    if (!append_integer(output, canonical.commands.size())) {
        return std::unexpected(make_error(ReplayErrorCode::InvalidCount, "could not encode command count"));
    }
    output.push_back('\n');

    for (const auto& command : canonical.commands) {
        output += "command ";
        if (!append_integer(output, command.key.tick)) {
            return std::unexpected(make_error(ReplayErrorCode::InvalidInteger, "could not encode command tick"));
        }
        output.push_back(' ');
        if (!append_integer(output, command.key.sequence)) {
            return std::unexpected(make_error(ReplayErrorCode::InvalidInteger, "could not encode command sequence"));
        }
        const bool encoded = std::visit(
            [&output](const auto& payload) -> bool {
                using Payload = std::remove_cvref_t<decltype(payload)>;
                if constexpr (std::same_as<Payload, ApplyImpulseCommandV1>) {
                    output += " impulse ";
                    if (!append_integer(output, payload.particle_id)) {
                        return false;
                    }
                    output.push_back(' ');
                    if (!append_floating_point(output, payload.impulse_x_kilogram_metres_per_second)) {
                        return false;
                    }
                    output.push_back(' ');
                    return append_floating_point(output, payload.impulse_y_kilogram_metres_per_second);
                } else if constexpr (std::same_as<Payload, RemoveGravityOperatorCommandV1>) {
                    output += " remove_operator ";
                    return append_integer(output, payload.operator_id);
                } else {
                    const auto& gravity_operator = payload.gravity_operator;
                    output += " install_operator ";
                    if (!append_integer(output, gravity_operator.id)) {
                        return false;
                    }
                    output.push_back(' ');
                    if (!append_integer(output, gravity_operator.priority)) {
                        return false;
                    }
                    output.push_back(' ');
                    if (!append_floating_point(output, gravity_operator.support_center_x_metres)) {
                        return false;
                    }
                    output.push_back(' ');
                    if (!append_floating_point(output, gravity_operator.support_center_y_metres)) {
                        return false;
                    }
                    output.push_back(' ');
                    if (!append_floating_point(output, gravity_operator.support_radius_metres)) {
                        return false;
                    }
                    return std::visit(
                        [&output](const auto& transform) -> bool {
                            using Transform = std::remove_cvref_t<decltype(transform)>;
                            if constexpr (std::same_as<Transform, serialization::RotateGravityV1>) {
                                output += " rotate_quarter_turns ";
                                return append_integer(
                                    output,
                                    static_cast<int>(transform.counterclockwise_quarter_turns));
                            } else if constexpr (std::same_as<Transform, serialization::ScaleGravityV1>) {
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
                }
            },
            command.payload);
        if (!encoded) {
            return std::unexpected(make_error(
                ReplayErrorCode::InvalidFloatingPoint,
                "could not encode replay command",
                0,
                0,
                command.key));
        }
        output.push_back('\n');
    }
    output += "end\n";
    return output;
}

std::expected<ReplayV1, ReplayError> decode_replay_v1(
    std::string_view encoded,
    ReplayDecodeLimits limits)
{
    if (encoded.size() > limits.maximum_input_bytes) {
        return std::unexpected(make_error(
            ReplayErrorCode::InputLimitExceeded,
            "replay input exceeds the caller-supplied byte limit"));
    }
    ReplayReader reader(encoded);
    auto magic = reader.take("replay header");
    if (!magic) {
        return std::unexpected(magic.error());
    }
    if (magic->text != replay_magic) {
        return std::unexpected(make_error(
            ReplayErrorCode::InvalidHeader,
            "invalid replay header '" + std::string(magic->text) + "'",
            magic->line,
            magic->column));
    }
    auto version = read_integer<std::uint32_t>(reader, "schema version");
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version != replay_schema_version_v1) {
        return std::unexpected(make_error(
            ReplayErrorCode::UnsupportedSchemaVersion,
            "unsupported replay schema version " + std::to_string(*version)));
    }
    if (auto marker = reader.expect("config"); !marker) {
        return std::unexpected(marker.error());
    }
    auto base_x = read_floating_point(reader, "base gravity x component");
    if (!base_x) {
        return std::unexpected(base_x.error());
    }
    auto base_y = read_floating_point(reader, "base gravity y component");
    if (!base_y) {
        return std::unexpected(base_y.error());
    }
    auto fixed_dt = read_floating_point(reader, "fixed time step");
    if (!fixed_dt) {
        return std::unexpected(fixed_dt.error());
    }
    auto end_tick = read_integer<std::uint64_t>(reader, "end tick");
    if (!end_tick) {
        return std::unexpected(end_tick.error());
    }
    if (auto marker = reader.expect("snapshot_bytes"); !marker) {
        return std::unexpected(marker.error());
    }
    auto snapshot_size = read_count(
        reader,
        "snapshot byte count",
        limits.snapshot.maximum_input_bytes,
        ReplayErrorCode::SnapshotPayloadLimitExceeded);
    if (!snapshot_size) {
        return std::unexpected(snapshot_size.error());
    }
    auto snapshot_payload = reader.take_payload(*snapshot_size);
    if (!snapshot_payload) {
        return std::unexpected(snapshot_payload.error());
    }
    auto snapshot = serialization::decode_snapshot_v1(*snapshot_payload, limits.snapshot);
    if (!snapshot) {
        return std::unexpected(snapshot_error(
            ReplayErrorCode::InvalidSnapshot,
            "embedded initial snapshot could not be decoded",
            snapshot.error()));
    }
    if (auto marker = reader.expect("commands"); !marker) {
        return std::unexpected(marker.error());
    }
    auto command_count = read_count(
        reader,
        "command count",
        limits.maximum_commands,
        ReplayErrorCode::CommandLimitExceeded);
    if (!command_count) {
        return std::unexpected(command_count.error());
    }

    ReplayV1 replay;
    replay.initial_snapshot = std::move(*snapshot);
    replay.base_gravity_x_metres_per_second_squared = *base_x;
    replay.base_gravity_y_metres_per_second_squared = *base_y;
    replay.fixed_dt_seconds = *fixed_dt;
    replay.end_tick = *end_tick;
    replay.commands.reserve(*command_count);

    for (std::size_t index = 0; index < *command_count; ++index) {
        static_cast<void>(index);
        if (auto marker = reader.expect("command"); !marker) {
            return std::unexpected(marker.error());
        }
        auto tick = read_integer<std::uint64_t>(reader, "command tick");
        if (!tick) {
            return std::unexpected(tick.error());
        }
        auto sequence = read_integer<std::uint64_t>(reader, "command sequence");
        if (!sequence) {
            return std::unexpected(sequence.error());
        }
        auto kind = reader.take("command kind");
        if (!kind) {
            return std::unexpected(kind.error());
        }

        CommandPayloadV1 payload;
        if (kind->text == "impulse") {
            auto particle_id = read_integer<std::uint64_t>(reader, "particle ID");
            if (!particle_id) {
                return std::unexpected(particle_id.error());
            }
            auto impulse_x = read_floating_point(reader, "impulse x component");
            if (!impulse_x) {
                return std::unexpected(impulse_x.error());
            }
            auto impulse_y = read_floating_point(reader, "impulse y component");
            if (!impulse_y) {
                return std::unexpected(impulse_y.error());
            }
            payload = ApplyImpulseCommandV1{*particle_id, *impulse_x, *impulse_y};
        } else if (kind->text == "remove_operator") {
            auto operator_id = read_integer<std::uint64_t>(reader, "gravity operator ID");
            if (!operator_id) {
                return std::unexpected(operator_id.error());
            }
            payload = RemoveGravityOperatorCommandV1{*operator_id};
        } else if (kind->text == "install_operator") {
            auto operator_id = read_integer<std::uint64_t>(reader, "gravity operator ID");
            if (!operator_id) {
                return std::unexpected(operator_id.error());
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
            auto transform_kind = reader.take("gravity transform kind");
            if (!transform_kind) {
                return std::unexpected(transform_kind.error());
            }
            serialization::GravityTransformV1 transform;
            if (transform_kind->text == "rotate_quarter_turns") {
                auto turns = read_integer<int>(reader, "counterclockwise quarter turns");
                if (!turns) {
                    return std::unexpected(turns.error());
                }
                constexpr auto minimum_turns = static_cast<int>(std::numeric_limits<std::int8_t>::min());
                constexpr auto maximum_turns = static_cast<int>(std::numeric_limits<std::int8_t>::max());
                if (*turns < minimum_turns || *turns > maximum_turns) {
                    return std::unexpected(make_error(
                        ReplayErrorCode::InvalidInteger,
                        "counterclockwise quarter turns must fit in a signed byte",
                        transform_kind->line,
                        transform_kind->column));
                }
                transform = serialization::RotateGravityV1{static_cast<std::int8_t>(*turns)};
            } else if (transform_kind->text == "scale") {
                auto factor = read_floating_point(reader, "gravity scale factor");
                if (!factor) {
                    return std::unexpected(factor.error());
                }
                transform = serialization::ScaleGravityV1{*factor};
            } else if (transform_kind->text == "offset") {
                auto offset_x = read_floating_point(reader, "gravity x offset");
                if (!offset_x) {
                    return std::unexpected(offset_x.error());
                }
                auto offset_y = read_floating_point(reader, "gravity y offset");
                if (!offset_y) {
                    return std::unexpected(offset_y.error());
                }
                transform = serialization::OffsetGravityV1{*offset_x, *offset_y};
            } else {
                return std::unexpected(make_error(
                    ReplayErrorCode::UnexpectedToken,
                    "unknown gravity transform kind '" + std::string(transform_kind->text) + "'",
                    transform_kind->line,
                    transform_kind->column));
            }
            payload = InstallGravityOperatorCommandV1{serialization::GravityOperatorRecordV1{
                *operator_id,
                *priority,
                *center_x,
                *center_y,
                *radius,
                std::move(transform)}};
        } else {
            return std::unexpected(make_error(
                ReplayErrorCode::UnexpectedToken,
                "unknown command kind '" + std::string(kind->text) + "'",
                kind->line,
                kind->column));
        }
        replay.commands.push_back(CommandV1{CommandKeyV1{*tick, *sequence}, std::move(payload)});
    }
    if (auto marker = reader.expect("end"); !marker) {
        return std::unexpected(marker.error());
    }
    if (reader.has_more()) {
        auto trailing = reader.take("end of replay");
        if (!trailing) {
            return std::unexpected(trailing.error());
        }
        return std::unexpected(make_error(
            ReplayErrorCode::TrailingData,
            "unexpected trailing token '" + std::string(trailing->text) + "'",
            trailing->line,
            trailing->column));
    }

    canonicalize(replay);
    auto validity = validate_replay_v1(replay);
    if (!validity) {
        return std::unexpected(validity.error());
    }
    return replay;
}

std::expected<ReplayRuntimeState2, ReplayError> restore_replay_initial_state_v1(const ReplayV1& replay)
{
    if (replay.schema_version != replay_schema_version_v1) {
        return std::unexpected(make_error(
            ReplayErrorCode::UnsupportedSchemaVersion,
            "unsupported Replay V1 schema version"));
    }
    const auto snapshot_validity = serialization::validate_snapshot_v1(replay.initial_snapshot);
    if (!snapshot_validity) {
        return std::unexpected(snapshot_error(
            ReplayErrorCode::InvalidSnapshot,
            "invalid initial snapshot",
            snapshot_validity.error()));
    }
    return restore_validated(replay);
}

std::expected<serialization::SnapshotV1, serialization::SnapshotError>
ReplayRunResult2::capture_snapshot_v1() const
{
    return serialization::capture_snapshot_v1(
        final_state.world,
        final_state.gravity_operators);
}

std::expected<ReplayRunResult2, ReplayError> run_replay_v1(
    const ReplayV1& replay,
    ReplayExecutionBudget budget)
{
    if (replay.commands.size() > budget.maximum_commands) {
        return std::unexpected(make_error(
            ReplayErrorCode::CommandLimitExceeded,
            "replay command count exceeds the caller-supplied execution budget"));
    }
    auto validity = validate_replay_v1(replay);
    if (!validity) {
        return std::unexpected(validity.error());
    }
    const auto requested_steps = replay.end_tick - replay.initial_snapshot.world_tick;
    if (requested_steps > budget.maximum_steps) {
        return std::unexpected(make_error(
            ReplayErrorCode::StepBudgetExceeded,
            "replay step count exceeds the caller-supplied execution budget"));
    }
    auto state = restore_validated(replay);
    if (!state) {
        return std::unexpected(state.error());
    }
    auto commands = replay.commands;
    std::ranges::sort(commands, command_less);

    const fields::GravityVector<2> base_gravity{
        units::metres_per_second_squared(replay.base_gravity_x_metres_per_second_squared),
        units::metres_per_second_squared(replay.base_gravity_y_metres_per_second_squared)};
    ReplayGravityField2 gravity{base_gravity, state->gravity_operators};
    const solvers::NewtonianParticleSolver2 solver;
    const auto fixed_dt = units::seconds(replay.fixed_dt_seconds);
    std::size_t command_index = 0;
    std::uint64_t executed_steps = 0;

    while (state->world.tick < replay.end_tick) {
        while (command_index < commands.size() && commands[command_index].key.tick == state->world.tick) {
            auto executed = execute_command(*state, commands[command_index]);
            if (!executed) {
                return std::unexpected(executed.error());
            }
            ++command_index;
        }
        auto step = solver.advance(state->world, gravity, fixed_dt);
        if (!step) {
            auto error = make_error(
                ReplayErrorCode::SolverFailed,
                "Newtonian replay integration step failed",
                0,
                0,
                CommandKeyV1{state->world.tick, 0});
            error.solver_cause = step.error();
            return std::unexpected(std::move(error));
        }
        ++executed_steps;
    }

    return ReplayRunResult2{std::move(*state), executed_steps};
}

}  // namespace principia::replay
