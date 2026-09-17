#pragma once

#include <principia/operators/gravity_operator.hpp>
#include <principia/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace principia::serialization {

inline constexpr std::uint32_t snapshot_schema_version_v1 = 1;

enum class SnapshotErrorCode : std::uint8_t {
    UnexpectedEnd,
    UnexpectedToken,
    InvalidHeader,
    UnsupportedSchemaVersion,
    InvalidInteger,
    InvalidFloatingPoint,
    InvalidEnum,
    InvalidCount,
    InvalidIdentifier,
    DuplicateIdentifier,
    NonFiniteValue,
    InvalidRadius,
    IncompatibleGravityTransform,
    TrailingData,
    InputLimitExceeded,
    ParticleLimitExceeded,
    GravityOperatorLimitExceeded,
    NegativeSimulationTime,
    NonPositiveMass,
    FixedParticleHasMomentum,
    RevisionExhausted,
    InvalidRevision,
};

struct SnapshotError {
    SnapshotErrorCode code{};
    std::size_t line{};
    std::size_t column{};
    std::string message;

    friend bool operator==(const SnapshotError&, const SnapshotError&) = default;
};

// These enums are part of the persistent V1 schema. Their values must never be
// repurposed, even if the corresponding runtime enums change in the future.
enum class ParticleConstraintV1 : std::uint8_t {
    Free = 0,
    Fixed = 1,
};

enum class GravityCompositionPolicyV1 : std::uint8_t {
    Additive = 0,
    Multiplicative = 1,
    ReplaceByPriority = 2,
    TransformChain = 3,
    ConstraintResolved = 4,
};

struct ParticleRecordV1 {
    std::uint64_t id{};
    double position_x_metres{};
    double position_y_metres{};
    double momentum_x_kilogram_metres_per_second{};
    double momentum_y_kilogram_metres_per_second{};
    double rest_mass_kilograms{};
    std::uint32_t material_id{};
    ParticleConstraintV1 constraint{ParticleConstraintV1::Free};

    friend bool operator==(const ParticleRecordV1&, const ParticleRecordV1&) = default;
};

struct RotateGravityV1 {
    std::int8_t counterclockwise_quarter_turns{};

    friend bool operator==(const RotateGravityV1&, const RotateGravityV1&) = default;
};

struct ScaleGravityV1 {
    double dimensionless_factor{1.0};

    friend bool operator==(const ScaleGravityV1&, const ScaleGravityV1&) = default;
};

struct OffsetGravityV1 {
    double x_metres_per_second_squared{};
    double y_metres_per_second_squared{};

    friend bool operator==(const OffsetGravityV1&, const OffsetGravityV1&) = default;
};

using GravityTransformV1 = std::variant<RotateGravityV1, ScaleGravityV1, OffsetGravityV1>;

struct GravityOperatorRecordV1 {
    std::uint64_t id{};
    std::int32_t priority{};
    double support_center_x_metres{};
    double support_center_y_metres{};
    double support_radius_metres{};
    GravityTransformV1 transform;

    friend bool operator==(const GravityOperatorRecordV1&, const GravityOperatorRecordV1&) = default;
};

// SnapshotV1 is a deliberately bounded persistence DTO for the simulation clock,
// particles, particle revision, and gravity-operator graph. It is not a complete
// world save: boundaries, material definitions, chunks, the base field, and game
// state are outside V1. Raw doubles are canonical SI scalars by schema definition;
// runtime physics APIs continue to use mp-units.
struct SnapshotV1 {
    std::uint32_t schema_version{snapshot_schema_version_v1};
    std::uint64_t world_tick{};
    double simulation_time_seconds{};
    std::uint64_t particle_revision{};
    std::vector<ParticleRecordV1> particles;
    GravityCompositionPolicyV1 gravity_composition_policy{GravityCompositionPolicyV1::TransformChain};
    std::vector<GravityOperatorRecordV1> gravity_operators;

    friend bool operator==(const SnapshotV1&, const SnapshotV1&) = default;
};

struct SnapshotDecodeLimits {
    std::size_t maximum_input_bytes{64U * 1024U * 1024U};
    std::size_t maximum_particles{1'000'000};
    std::size_t maximum_gravity_operators{1'000'000};
};

[[nodiscard]] std::expected<void, SnapshotError> validate_snapshot_v1(const SnapshotV1& snapshot);

[[nodiscard]] std::expected<SnapshotV1, SnapshotError> capture_snapshot_v1(
    const world::WorldState2& world,
    const operators::GravityOperatorGraph& gravity_operators);

[[nodiscard]] std::expected<std::string, SnapshotError> encode_snapshot_v1(const SnapshotV1& snapshot);

[[nodiscard]] std::expected<SnapshotV1, SnapshotError> decode_snapshot_v1(
    std::string_view encoded,
    SnapshotDecodeLimits limits = {});

}  // namespace principia::serialization
