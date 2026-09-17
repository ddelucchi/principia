#pragma once

#include <principia/operators/gravity_operator.hpp>
#include <principia/serialization/snapshot.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/world/world.hpp>

#include <cstddef>
#include <compare>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace principia::replay {

inline constexpr std::uint32_t replay_schema_version_v1 = 1;

struct CommandKeyV1 {
    std::uint64_t tick{};
    std::uint64_t sequence{};

    friend auto operator<=>(const CommandKeyV1&, const CommandKeyV1&) = default;
};

struct ApplyImpulseCommandV1 {
    std::uint64_t particle_id{};
    double impulse_x_kilogram_metres_per_second{};
    double impulse_y_kilogram_metres_per_second{};

    friend bool operator==(const ApplyImpulseCommandV1&, const ApplyImpulseCommandV1&) = default;
};

struct InstallGravityOperatorCommandV1 {
    serialization::GravityOperatorRecordV1 gravity_operator;

    friend bool operator==(const InstallGravityOperatorCommandV1&, const InstallGravityOperatorCommandV1&) = default;
};

struct RemoveGravityOperatorCommandV1 {
    std::uint64_t operator_id{};

    friend bool operator==(const RemoveGravityOperatorCommandV1&, const RemoveGravityOperatorCommandV1&) = default;
};

using CommandPayloadV1 = std::variant<
    ApplyImpulseCommandV1,
    InstallGravityOperatorCommandV1,
    RemoveGravityOperatorCommandV1>;

struct CommandV1 {
    CommandKeyV1 key;
    CommandPayloadV1 payload;

    friend bool operator==(const CommandV1&, const CommandV1&) = default;
};

// ReplayV1 is scoped to SnapshotV1 plus a constant base-gravity vector and the
// command kinds declared below. It is not a complete world save. Its raw doubles
// are canonical SI scalars by schema definition; reconstructed runtime state uses
// typed quantities.
struct ReplayV1 {
    std::uint32_t schema_version{replay_schema_version_v1};
    serialization::SnapshotV1 initial_snapshot;
    double base_gravity_x_metres_per_second_squared{};
    double base_gravity_y_metres_per_second_squared{};
    double fixed_dt_seconds{};
    std::uint64_t end_tick{};
    std::vector<CommandV1> commands;

    friend bool operator==(const ReplayV1&, const ReplayV1&) = default;
};

enum class ReplayErrorCode : std::uint8_t {
    InvalidHeader,
    UnsupportedSchemaVersion,
    UnexpectedEnd,
    UnexpectedToken,
    InvalidInteger,
    InvalidFloatingPoint,
    InvalidCount,
    NonFiniteValue,
    NonPositiveTimeStep,
    EndTickBeforeInitialTick,
    CommandOutsideTickRange,
    DuplicateCommandKey,
    InvalidIdentifier,
    InvalidSnapshot,
    InvalidGravityOperator,
    MissingParticle,
    DuplicateGravityOperator,
    MissingGravityOperator,
    CommandProducedNonFiniteState,
    SnapshotRestoreFailed,
    SolverFailed,
    SnapshotCaptureFailed,
    TrailingData,
    InputLimitExceeded,
    SnapshotPayloadLimitExceeded,
    CommandLimitExceeded,
    StepBudgetExceeded,
    ConstrainedParticle,
    RevisionExhausted,
    ParticleMutationFailed,
};

struct ReplayError {
    ReplayErrorCode code{};
    std::size_t line{};
    std::size_t column{};
    std::uint64_t tick{};
    std::uint64_t sequence{};
    std::string message;
    std::optional<serialization::SnapshotErrorCode> snapshot_cause;
    std::optional<solvers::NewtonianStepError> solver_cause;

    friend bool operator==(const ReplayError&, const ReplayError&) = default;
};

struct ReplayRuntimeState2 {
    world::WorldState2 world;
    operators::GravityOperatorGraph gravity_operators;
};

struct ReplayRunResult2 {
    ReplayRuntimeState2 final_state;
    std::uint64_t executed_steps{};

    [[nodiscard]] std::expected<serialization::SnapshotV1, serialization::SnapshotError>
    capture_snapshot_v1() const;
};

struct ReplayDecodeLimits {
    std::size_t maximum_input_bytes{128U * 1024U * 1024U};
    serialization::SnapshotDecodeLimits snapshot;
    std::size_t maximum_commands{1'000'000};
};

struct ReplayExecutionBudget {
    std::uint64_t maximum_steps{10'000'000};
    std::size_t maximum_commands{1'000'000};
};

[[nodiscard]] std::expected<void, ReplayError> validate_replay_v1(const ReplayV1& replay);

[[nodiscard]] std::expected<std::string, ReplayError> encode_replay_v1(const ReplayV1& replay);

[[nodiscard]] std::expected<ReplayV1, ReplayError> decode_replay_v1(
    std::string_view encoded,
    ReplayDecodeLimits limits = {});

[[nodiscard]] std::expected<ReplayRuntimeState2, ReplayError> restore_replay_initial_state_v1(
    const ReplayV1& replay);

// Commands for tick T execute in sequence order immediately before the fixed
// integration step from T to T + 1. The result owns the single authoritative
// final runtime state; callers may explicitly capture its bounded V1 projection.
[[nodiscard]] std::expected<ReplayRunResult2, ReplayError> run_replay_v1(
    const ReplayV1& replay,
    ReplayExecutionBudget budget = {});

}  // namespace principia::replay
