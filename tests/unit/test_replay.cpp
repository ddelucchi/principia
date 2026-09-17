#include "../test_support.hpp"

#include <principia/game/reality_test_001.hpp>
#include <principia/replay/replay.hpp>
#include <principia/serialization/snapshot.hpp>

#include <algorithm>
#include <limits>

void test_replay()
{
    using namespace principia;
    using tests::require;

    auto scenario = game::make_reality_test_001();
    const auto captured = serialization::capture_snapshot_v1(scenario.world, scenario.gravity.operator_graph());
    require(captured.has_value() && captured->gravity_operators.size() == 1,
            "the replay fixture needs a canonical initial snapshot and operator");

    replay::ReplayV1 stream;
    stream.initial_snapshot = *captured;
    stream.base_gravity_x_metres_per_second_squared = 0.0;
    stream.base_gravity_y_metres_per_second_squared = -9.81;
    stream.fixed_dt_seconds = 1.0 / 120.0;
    stream.end_tick = 30;
    stream.commands = {
        {{10, 1}, replay::InstallGravityOperatorCommandV1{captured->gravity_operators.front()}},
        {{0, 2}, replay::RemoveGravityOperatorCommandV1{1}},
        {{0, 1}, replay::ApplyImpulseCommandV1{2, 1.0, -0.5}},
    };

    require(replay::validate_replay_v1(stream).has_value(), "a valid scheduled causal command stream should pass");
    const auto encoded = replay::encode_replay_v1(stream);
    require(encoded.has_value(), "Replay V1 should encode canonically");
    const auto decoded = replay::decode_replay_v1(*encoded);
    require(decoded.has_value(), "Replay V1 should decode its own canonical representation");
    const auto reencoded = replay::encode_replay_v1(*decoded);
    require(reencoded.has_value() && *reencoded == *encoded,
            "Replay V1 encoding must be byte-stable independent of command insertion order");

    const auto restored = replay::restore_replay_initial_state_v1(*decoded);
    require(restored.has_value() && restored->world.tick == captured->world_tick &&
                restored->world.particles.revision() == captured->particle_revision,
            "replay restore must preserve the persistent clock and particle revision");
    auto invalid_future = *decoded;
    invalid_future.commands.front().key.tick = invalid_future.end_tick;
    require(!replay::validate_replay_v1(invalid_future).has_value() &&
                replay::restore_replay_initial_state_v1(invalid_future).has_value(),
            "restoring valid initial state must not depend on whether an unrelated future command stream is executable");

    const auto first = replay::run_replay_v1(*decoded);
    const auto second = replay::run_replay_v1(*decoded);
    require(first.has_value() && second.has_value(), "the same valid replay must execute repeatedly");
    require(first->executed_steps == 30 && second->executed_steps == 30,
            "the runner must execute exactly the declared tick interval");
    const auto first_snapshot = first->capture_snapshot_v1();
    const auto second_snapshot = second->capture_snapshot_v1();
    require(first_snapshot.has_value() && second_snapshot.has_value(),
            "replay results must expose one authoritative state that can be captured explicitly");
    const auto final_a = serialization::encode_snapshot_v1(*first_snapshot);
    const auto final_b = serialization::encode_snapshot_v1(*second_snapshot);
    require(final_a.has_value() && final_b.has_value() && *final_a == *final_b,
            "identical commands and initial state must produce byte-identical canonical state");

    auto changed = *decoded;
    auto* changed_impulse = std::get_if<replay::ApplyImpulseCommandV1>(&changed.commands.front().payload);
    require(changed_impulse != nullptr, "canonical command order should place the impulse first");
    changed_impulse->impulse_x_kilogram_metres_per_second += 0.25;
    const auto different = replay::run_replay_v1(changed);
    require(different.has_value(), "a changed but valid cause should still replay");
    const auto different_snapshot = different->capture_snapshot_v1();
    require(different_snapshot.has_value(), "changed replay state must remain capturable");
    const auto final_changed = serialization::encode_snapshot_v1(*different_snapshot);
    require(final_changed.has_value() && *final_changed != *final_a,
            "changing one causal command must change the canonical result");

    auto duplicate = *decoded;
    duplicate.commands.push_back(duplicate.commands.front());
    const auto duplicate_error = replay::validate_replay_v1(duplicate);
    require(!duplicate_error && duplicate_error.error().code == replay::ReplayErrorCode::DuplicateCommandKey,
            "duplicate tick/sequence keys must be rejected before mutation");

    auto missing = *decoded;
    auto* missing_impulse = std::get_if<replay::ApplyImpulseCommandV1>(&missing.commands.front().payload);
    require(missing_impulse != nullptr, "missing-target fixture must address an impulse command");
    missing_impulse->particle_id = 999;
    const auto missing_error = replay::run_replay_v1(missing);
    require(!missing_error && missing_error.error().code == replay::ReplayErrorCode::MissingParticle,
            "commands may not address absent canonical entities");

    auto nonfinite = *decoded;
    auto* nonfinite_impulse = std::get_if<replay::ApplyImpulseCommandV1>(&nonfinite.commands.front().payload);
    require(nonfinite_impulse != nullptr, "non-finite fixture must address an impulse command");
    nonfinite_impulse->impulse_y_kilogram_metres_per_second = std::numeric_limits<double>::infinity();
    const auto nonfinite_error = replay::validate_replay_v1(nonfinite);
    require(!nonfinite_error && nonfinite_error.error().code == replay::ReplayErrorCode::NonFiniteValue,
            "persistence DTOs must reject non-finite SI scalars");

    auto invalid_dt = *decoded;
    invalid_dt.fixed_dt_seconds = 0.0;
    const auto dt_error = replay::validate_replay_v1(invalid_dt);
    require(!dt_error && dt_error.error().code == replay::ReplayErrorCode::NonPositiveTimeStep,
            "replay integration requires a positive fixed time step");
}
