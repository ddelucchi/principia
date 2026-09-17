#include "../test_support.hpp"

#include <principia/replay/replay.hpp>
#include <principia/serialization/snapshot.hpp>

#include <exception>
#include <iostream>
#include <limits>
#include <variant>

namespace {

[[nodiscard]] principia::serialization::SnapshotV1 make_snapshot()
{
    using namespace principia;

    serialization::SnapshotV1 snapshot;
    snapshot.particle_revision = 1;
    snapshot.particles.push_back(serialization::ParticleRecordV1{
        1,
        2.0,
        3.0,
        0.0,
        0.0,
        1.0,
        1,
        serialization::ParticleConstraintV1::Free});
    snapshot.gravity_operators.push_back(serialization::GravityOperatorRecordV1{
        1,
        10,
        0.0,
        0.0,
        20.0,
        serialization::RotateGravityV1{5}});
    return snapshot;
}

void test_snapshot_validation_and_limits()
{
    using namespace principia;
    using tests::require;

    auto negative_time = make_snapshot();
    negative_time.simulation_time_seconds = -1.0;
    const auto negative_time_error = serialization::validate_snapshot_v1(negative_time);
    require(
        !negative_time_error &&
            negative_time_error.error().code == serialization::SnapshotErrorCode::NegativeSimulationTime,
        "Snapshot V1 must reject negative simulation time explicitly");

    auto zero_mass = make_snapshot();
    zero_mass.particles.front().rest_mass_kilograms = 0.0;
    const auto zero_mass_error = serialization::validate_snapshot_v1(zero_mass);
    require(
        !zero_mass_error && zero_mass_error.error().code == serialization::SnapshotErrorCode::NonPositiveMass,
        "Snapshot V1 must reject zero rest mass explicitly");

    auto negative_mass = make_snapshot();
    negative_mass.particles.front().rest_mass_kilograms = -1.0;
    const auto negative_mass_error = serialization::validate_snapshot_v1(negative_mass);
    require(
        !negative_mass_error && negative_mass_error.error().code == serialization::SnapshotErrorCode::NonPositiveMass,
        "Snapshot V1 must reject negative rest mass explicitly");

    auto moving_fixed = make_snapshot();
    moving_fixed.particles.front().constraint = serialization::ParticleConstraintV1::Fixed;
    moving_fixed.particles.front().momentum_x_kilogram_metres_per_second = 1.0;
    const auto moving_fixed_error = serialization::validate_snapshot_v1(moving_fixed);
    require(
        !moving_fixed_error &&
            moving_fixed_error.error().code == serialization::SnapshotErrorCode::FixedParticleHasMomentum,
        "Snapshot V1 must reject fixed particles with canonical momentum");

    auto exhausted_revision = make_snapshot();
    exhausted_revision.particle_revision = std::numeric_limits<std::uint64_t>::max();
    const auto exhausted_revision_error = serialization::validate_snapshot_v1(exhausted_revision);
    require(
        !exhausted_revision_error &&
            exhausted_revision_error.error().code == serialization::SnapshotErrorCode::RevisionExhausted,
        "Snapshot V1 must reject an exhausted particle revision");

    auto impossible_revision = make_snapshot();
    impossible_revision.particle_revision = 0;
    const auto impossible_revision_error = serialization::validate_snapshot_v1(impossible_revision);
    require(
        !impossible_revision_error &&
            impossible_revision_error.error().code == serialization::SnapshotErrorCode::InvalidRevision,
        "Snapshot V1 must reject a particle source stamp predating its live records");

    auto nonfinite_operator = make_snapshot();
    nonfinite_operator.gravity_operators.front().transform = serialization::ScaleGravityV1{
        std::numeric_limits<double>::infinity()};
    const auto nonfinite_error = serialization::validate_snapshot_v1(nonfinite_operator);
    require(
        !nonfinite_error && nonfinite_error.error().code == serialization::SnapshotErrorCode::NonFiniteValue,
        "Snapshot V1 must reject non-finite operator scalars");
    const auto nonfinite_encode_error = serialization::encode_snapshot_v1(nonfinite_operator);
    require(
        !nonfinite_encode_error &&
            nonfinite_encode_error.error().code == serialization::SnapshotErrorCode::NonFiniteValue,
        "Snapshot V1 codec must not emit non-finite operator scalars");

    const auto encoded = serialization::encode_snapshot_v1(make_snapshot());
    require(encoded.has_value(), "valid Snapshot V1 fixture must encode");
    const auto decoded = serialization::decode_snapshot_v1(*encoded);
    require(decoded.has_value(), "valid Snapshot V1 fixture must decode");
    const auto* normalized = std::get_if<serialization::RotateGravityV1>(
        &decoded->gravity_operators.front().transform);
    require(
        normalized != nullptr && normalized->counterclockwise_quarter_turns == 1,
        "five quarter turns must canonicalize to one");

    auto equivalent = make_snapshot();
    std::get<serialization::RotateGravityV1>(equivalent.gravity_operators.front().transform)
        .counterclockwise_quarter_turns = -3;
    const auto equivalent_encoded = serialization::encode_snapshot_v1(equivalent);
    require(
        equivalent_encoded.has_value() && *equivalent_encoded == *encoded,
        "semantically equivalent quarter turns must have one canonical encoding");

    auto input_limits = serialization::SnapshotDecodeLimits{};
    input_limits.maximum_input_bytes = encoded->size() - 1;
    const auto input_limit_error = serialization::decode_snapshot_v1(*encoded, input_limits);
    require(
        !input_limit_error &&
            input_limit_error.error().code == serialization::SnapshotErrorCode::InputLimitExceeded,
        "snapshot byte limits must reject input before parsing");

    auto particle_limits = serialization::SnapshotDecodeLimits{};
    particle_limits.maximum_particles = 0;
    const auto particle_limit_error = serialization::decode_snapshot_v1(*encoded, particle_limits);
    require(
        !particle_limit_error &&
            particle_limit_error.error().code == serialization::SnapshotErrorCode::ParticleLimitExceeded,
        "snapshot particle limits must have a distinct error code");

    auto operator_limits = serialization::SnapshotDecodeLimits{};
    operator_limits.maximum_gravity_operators = 0;
    const auto operator_limit_error = serialization::decode_snapshot_v1(*encoded, operator_limits);
    require(
        !operator_limit_error &&
            operator_limit_error.error().code == serialization::SnapshotErrorCode::GravityOperatorLimitExceeded,
        "snapshot operator limits must have a distinct error code");
}

void test_replay_limits_and_budget()
{
    using namespace principia;
    using tests::require;

    const auto canonical_snapshot_text = serialization::encode_snapshot_v1(make_snapshot());
    require(canonical_snapshot_text.has_value(), "replay fixture snapshot must encode");
    const auto canonical_snapshot = serialization::decode_snapshot_v1(*canonical_snapshot_text);
    require(canonical_snapshot.has_value(), "replay fixture snapshot must decode");

    replay::ReplayV1 stream;
    stream.initial_snapshot = *canonical_snapshot;
    stream.base_gravity_y_metres_per_second_squared = -9.81;
    stream.fixed_dt_seconds = 0.1;
    stream.end_tick = 5;
    stream.commands.push_back(replay::CommandV1{
        replay::CommandKeyV1{1, 0},
        replay::InstallGravityOperatorCommandV1{serialization::GravityOperatorRecordV1{
            2,
            20,
            0.0,
            0.0,
            20.0,
            serialization::RotateGravityV1{-3}}}});

    const auto encoded = replay::encode_replay_v1(stream);
    require(encoded.has_value(), "valid Replay V1 fixture must encode");
    const auto decoded = replay::decode_replay_v1(*encoded);
    require(decoded.has_value(), "valid Replay V1 fixture must decode");
    const auto* install = std::get_if<replay::InstallGravityOperatorCommandV1>(
        &decoded->commands.front().payload);
    require(install != nullptr, "replay fixture command must remain an installation");
    const auto* normalized = std::get_if<serialization::RotateGravityV1>(
        &install->gravity_operator.transform);
    require(
        normalized != nullptr && normalized->counterclockwise_quarter_turns == 1,
        "replay operator commands must normalize equivalent rotations");

    auto input_limits = replay::ReplayDecodeLimits{};
    input_limits.maximum_input_bytes = encoded->size() - 1;
    const auto input_limit_error = replay::decode_replay_v1(*encoded, input_limits);
    require(
        !input_limit_error && input_limit_error.error().code == replay::ReplayErrorCode::InputLimitExceeded,
        "replay byte limits must reject input before parsing");

    auto snapshot_payload_limits = replay::ReplayDecodeLimits{};
    snapshot_payload_limits.snapshot.maximum_input_bytes = canonical_snapshot_text->size() - 1;
    const auto snapshot_payload_error = replay::decode_replay_v1(*encoded, snapshot_payload_limits);
    require(
        !snapshot_payload_error &&
            snapshot_payload_error.error().code == replay::ReplayErrorCode::SnapshotPayloadLimitExceeded,
        "embedded snapshot byte limits must have a distinct replay error code");

    auto command_limits = replay::ReplayDecodeLimits{};
    command_limits.maximum_commands = 0;
    const auto command_limit_error = replay::decode_replay_v1(*encoded, command_limits);
    require(
        !command_limit_error && command_limit_error.error().code == replay::ReplayErrorCode::CommandLimitExceeded,
        "replay command limits must have a distinct error code");

    auto nested_limits = replay::ReplayDecodeLimits{};
    nested_limits.snapshot.maximum_particles = 0;
    const auto nested_error = replay::decode_replay_v1(*encoded, nested_limits);
    require(
        !nested_error && nested_error.error().code == replay::ReplayErrorCode::InvalidSnapshot &&
            nested_error.error().snapshot_cause == serialization::SnapshotErrorCode::ParticleLimitExceeded,
        "replay errors must preserve the structured embedded-snapshot cause");

    const auto budget_error = replay::run_replay_v1(*decoded, replay::ReplayExecutionBudget{4});
    require(
        !budget_error && budget_error.error().code == replay::ReplayErrorCode::StepBudgetExceeded,
        "replay must reject over-budget work before execution");
    const auto exact_budget = replay::run_replay_v1(*decoded, replay::ReplayExecutionBudget{5});
    require(
        exact_budget.has_value() && exact_budget->executed_steps == 5,
        "replay must execute when the requested interval fits exactly within budget");
    auto command_budget = replay::ReplayExecutionBudget{};
    command_budget.maximum_commands = 0;
    const auto command_budget_error = replay::run_replay_v1(*decoded, command_budget);
    require(
        !command_budget_error &&
            command_budget_error.error().code == replay::ReplayErrorCode::CommandLimitExceeded,
        "direct replay execution must bound command sorting independently of decode limits");

    auto nonfinite_base = *decoded;
    nonfinite_base.base_gravity_x_metres_per_second_squared =
        std::numeric_limits<double>::infinity();
    const auto nonfinite_base_error = replay::validate_replay_v1(nonfinite_base);
    require(
        !nonfinite_base_error && nonfinite_base_error.error().code == replay::ReplayErrorCode::NonFiniteValue,
        "Replay V1 must reject non-finite base-field scalars");
    const auto nonfinite_base_encode_error = replay::encode_replay_v1(nonfinite_base);
    require(
        !nonfinite_base_encode_error &&
            nonfinite_base_encode_error.error().code == replay::ReplayErrorCode::NonFiniteValue,
        "Replay V1 codec must not emit non-finite base-field scalars");

    auto negative_time = *decoded;
    negative_time.initial_snapshot.simulation_time_seconds = -1.0;
    const auto negative_time_error = replay::validate_replay_v1(negative_time);
    require(
        !negative_time_error && negative_time_error.error().code == replay::ReplayErrorCode::InvalidSnapshot &&
            negative_time_error.error().snapshot_cause ==
                serialization::SnapshotErrorCode::NegativeSimulationTime,
        "Replay V1 must retain the explicit negative-time snapshot cause");

    auto constrained = *decoded;
    constrained.initial_snapshot.particles.front().constraint =
        serialization::ParticleConstraintV1::Fixed;
    constrained.commands = {
        replay::CommandV1{
            replay::CommandKeyV1{0, 0},
            replay::ApplyImpulseCommandV1{1, 1.0, 0.0}},
    };
    const auto constrained_error = replay::run_replay_v1(constrained);
    require(
        !constrained_error && constrained_error.error().code == replay::ReplayErrorCode::ConstrainedParticle,
        "Replay V1 must reject constrained-particle impulses before mutation");
}

}  // namespace

int main()
{
    try {
        test_snapshot_validation_and_limits();
        test_replay_limits_and_budget();
        std::cout << "[PASS] persistence_edge_cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] persistence_edge_cases: " << error.what() << '\n';
        return 1;
    }
}
