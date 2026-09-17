#pragma once

#include <principia/core/deterministic_rng.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/materials/material.hpp>
#include <principia/materials/mechanical_response.hpp>
#include <principia/operators/gravity_operator.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/world/collider.hpp>
#include <principia/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace principia::serialization {

inline constexpr std::uint32_t foundation_checkpoint_schema_version_v2 = 2;
inline constexpr std::uint32_t foundation_inertial_frame_id_v2 = 1;
inline constexpr std::uint32_t mechanical_response_section_type_v2 = 1;
inline constexpr std::uint32_t disc_collider_section_type_v2 = 2;
inline constexpr std::uint32_t foundation_execution_configuration_section_type_v2 = 3;
inline constexpr std::uint32_t foundation_registry_section_revision_v2 = 1;
inline constexpr std::uint32_t foundation_execution_configuration_section_revision_v2 = 1;

enum class CheckpointErrorCode : std::uint8_t {
    UnexpectedEnd,
    UnexpectedToken,
    InvalidHeader,
    UnsupportedSchemaVersion,
    InvalidInteger,
    InvalidFloatingPoint,
    InvalidStringEncoding,
    InvalidEnum,
    InvalidCount,
    InvalidIdentifier,
    DuplicateIdentifier,
    NonFiniteValue,
    NegativeSimulationTime,
    NonPositiveMass,
    FixedParticleHasMomentum,
    InvalidDefinition,
    InvalidRevision,
    InvalidAllocatorState,
    UnresolvedMaterial,
    IncompatibleGravityTransform,
    InvalidGravityField,
    InvalidSimulationContract,
    UnsupportedSimulationContract,
    InvalidExecutionConfiguration,
    UnsupportedExecutionConfiguration,
    UnsupportedSeedMixerVersion,
    InputLimitExceeded,
    StringLimitExceeded,
    ParticleLimitExceeded,
    BoundaryLimitExceeded,
    BoundaryConditionLimitExceeded,
    MaterialLimitExceeded,
    CatalogEntryLimitExceeded,
    GravityOperatorLimitExceeded,
    MechanicalResponseLimitExceeded,
    ColliderLimitExceeded,
    ExtensionLimitExceeded,
    TrailingData,
};

struct CheckpointError {
    CheckpointErrorCode code{};
    std::size_t line{};
    std::size_t column{};
    std::string message;

    friend bool operator==(const CheckpointError&, const CheckpointError&) = default;
};

enum class ScalarFormatV2 : std::uint8_t {
    Ieee754Binary64 = 1,
};

enum class ByteOrderV2 : std::uint8_t {
    LittleEndian = 1,
};

enum class QuantityEncodingV2 : std::uint8_t {
    CanonicalSiScalars = 1,
};

enum class DeterminismGuaranteeV2 : std::uint8_t {
    BitwiseWithinBuild = 1,
    DeterministicWithinTolerance = 2,
};

enum class FloatingPointPolicyV2 : std::uint8_t {
    StrictIeee754 = 1,
    DeclaredContraction = 2,
};

enum class NonFinitePolicyV2 : std::uint8_t {
    RejectInputAndProposal = 1,
};

enum class IterationOrderPolicyV2 : std::uint8_t {
    CanonicalPersistentId = 1,
};

enum class AccumulationPolicyV2 : std::uint8_t {
    SerialCanonical = 1,
    CanonicalPairwiseTree = 2,
};

struct NumericAbiV2 {
    std::string id{"principia.ieee754.binary64.canonical-si"};
    std::uint32_t revision{1};
    ScalarFormatV2 scalar_format{ScalarFormatV2::Ieee754Binary64};
    ByteOrderV2 byte_order{ByteOrderV2::LittleEndian};
    QuantityEncodingV2 quantity_encoding{QuantityEncodingV2::CanonicalSiScalars};
    DeterminismGuaranteeV2 determinism{DeterminismGuaranteeV2::BitwiseWithinBuild};
    FloatingPointPolicyV2 floating_point{FloatingPointPolicyV2::StrictIeee754};
    NonFinitePolicyV2 non_finite{NonFinitePolicyV2::RejectInputAndProposal};
    IterationOrderPolicyV2 iteration_order{IterationOrderPolicyV2::CanonicalPersistentId};
    AccumulationPolicyV2 accumulation{AccumulationPolicyV2::SerialCanonical};

    friend bool operator==(const NumericAbiV2&, const NumericAbiV2&) = default;
};

struct SimulationContractV2 {
    std::uint32_t theory_id{};
    std::string integrator_id;
    std::uint32_t integrator_revision{};
    std::uint32_t validity_contract_revision{};
    std::uint32_t step_contract_revision{};
    std::uint32_t fixed_frame_id{};
    double fixed_step_seconds{};
    std::uint32_t world_topology_dimension{2};
    std::uint32_t physical_field_dimension{3};
    std::uint32_t spacetime_model_dimension{4};
    NumericAbiV2 numeric_abi;

    friend bool operator==(const SimulationContractV2&, const SimulationContractV2&) = default;
};

struct AllocatorCursorV2 {
    std::uint64_t next_identifier{1};
    bool exhausted{false};

    friend bool operator==(const AllocatorCursorV2&, const AllocatorCursorV2&) = default;
};

enum class ParticleConstraintV2 : std::uint8_t {
    Free = 0,
    Fixed = 1,
};

struct ParticleRecordV2 {
    std::uint64_t id{};
    double position_x_metres{};
    double position_y_metres{};
    double momentum_x_kilogram_metres_per_second{};
    double momentum_y_kilogram_metres_per_second{};
    double rest_mass_kilograms{};
    std::uint32_t material_id{};
    ParticleConstraintV2 constraint{ParticleConstraintV2::Free};

    friend bool operator==(const ParticleRecordV2&, const ParticleRecordV2&) = default;
};

enum class BoundaryConditionV2 : std::uint8_t {
    MechanicallyRigid = 0,
    ThermallyConductive = 1,
    ThermallyInsulating = 2,
    ElectricallyConductive = 3,
    ElectricallyInsulating = 4,
    OpticallyTransparent = 5,
    FluidImpermeable = 6,
};

struct BoundaryConditionRecordV2 {
    std::uint32_t channel_id{};
    BoundaryConditionV2 condition{BoundaryConditionV2::MechanicallyRigid};

    friend bool operator==(const BoundaryConditionRecordV2&, const BoundaryConditionRecordV2&) = default;
};

struct BoundaryRecordV2 {
    std::uint64_t id{};
    std::string name;
    double minimum_x_metres{};
    double minimum_y_metres{};
    double maximum_x_metres{};
    double maximum_y_metres{};
    std::vector<BoundaryConditionRecordV2> conditions;

    friend bool operator==(const BoundaryRecordV2&, const BoundaryRecordV2&) = default;
};

struct CatalogFamilyV2 {
    bool known{false};
    std::vector<std::uint32_t> identifiers;

    friend bool operator==(const CatalogFamilyV2&, const CatalogFamilyV2&) = default;
};

struct MaterialCatalogV2 {
    bool present{false};
    CatalogFamilyV2 compositions;
    CatalogFamilyV2 mechanical_models;
    CatalogFamilyV2 thermal_models;
    CatalogFamilyV2 electrical_models;
    CatalogFamilyV2 magnetic_models;
    CatalogFamilyV2 optical_models;
    CatalogFamilyV2 phase_models;

    friend bool operator==(const MaterialCatalogV2&, const MaterialCatalogV2&) = default;
};

struct MaterialRecordV2 {
    std::uint32_t id{};
    std::string name;
    std::uint32_t composition_id{};
    std::uint32_t mechanical_model_id{};
    std::uint32_t thermal_model_id{};
    std::uint32_t electrical_model_id{};
    std::uint32_t magnetic_model_id{};
    std::uint32_t optical_model_id{};
    std::uint32_t phase_model_id{};

    friend bool operator==(const MaterialRecordV2&, const MaterialRecordV2&) = default;
};

struct ConstantGravityFieldV2 {
    std::uint32_t field_type_id{fields::effective_newtonian_gravity_field_id.value()};
    std::string name{"EffectiveNewtonianGravityField"};
    std::uint8_t field_location{static_cast<std::uint8_t>(state::FieldLocation::CellCentered)};
    std::uint8_t storage_kind{static_cast<std::uint8_t>(fields::FieldStorageKind::Constant)};
    std::uint8_t physical_components{2};
    double x_metres_per_second_squared{};
    double y_metres_per_second_squared{};

    friend bool operator==(const ConstantGravityFieldV2&, const ConstantGravityFieldV2&) = default;
};

enum class GravityCompositionPolicyV2 : std::uint8_t {
    Additive = 0,
    Multiplicative = 1,
    ReplaceByPriority = 2,
    TransformChain = 3,
    ConstraintResolved = 4,
};

struct RotateGravityV2 {
    std::int8_t counterclockwise_quarter_turns{};
    friend bool operator==(const RotateGravityV2&, const RotateGravityV2&) = default;
};

struct ScaleGravityV2 {
    double dimensionless_factor{1.0};
    friend bool operator==(const ScaleGravityV2&, const ScaleGravityV2&) = default;
};

struct OffsetGravityV2 {
    double x_metres_per_second_squared{};
    double y_metres_per_second_squared{};
    friend bool operator==(const OffsetGravityV2&, const OffsetGravityV2&) = default;
};

using GravityTransformV2 = std::variant<RotateGravityV2, ScaleGravityV2, OffsetGravityV2>;

struct GravityOperatorRecordV2 {
    std::uint64_t id{};
    std::int32_t priority{};
    double support_center_x_metres{};
    double support_center_y_metres{};
    double support_radius_metres{};
    GravityTransformV2 transform;

    friend bool operator==(const GravityOperatorRecordV2&, const GravityOperatorRecordV2&) = default;
};

enum class ImpactResponseKindV2 : std::uint8_t {
    FrictionlessRestitution = 0,
};

struct MechanicalResponseRecordV2 {
    std::uint32_t id{};
    std::string name;
    ImpactResponseKindV2 impact_kind{ImpactResponseKindV2::FrictionlessRestitution};
    double normal_coefficient_of_restitution{1.0};

    friend bool operator==(const MechanicalResponseRecordV2&, const MechanicalResponseRecordV2&) = default;
};

struct DiscColliderRecordV2 {
    std::uint64_t particle_id{};
    double radius_metres{};

    friend bool operator==(const DiscColliderRecordV2&, const DiscColliderRecordV2&) = default;
};

// Fixed-width persistence representation for all runtime configuration that
// changes Newtonian proposal acceptance or contact execution. Native size_t
// budgets are encoded as u64 and range-checked before restoration.
struct FoundationExecutionConfigurationRecordV2 {
    bool contact_enabled{true};
    double invariant_speed_metres_per_second{299'792'458.0};
    double maximum_beta{0.01};
    std::uint32_t maximum_contact_events{64};
    std::uint64_t maximum_contact_bodies{1024};
    std::uint64_t maximum_contact_boundaries{1024};
    double contact_geometric_tolerance_metres{1.0e-12};
    double contact_simultaneous_time_tolerance_seconds{1.0e-12};

    friend bool operator==(const FoundationExecutionConfigurationRecordV2&,
                           const FoundationExecutionConfigurationRecordV2&) = default;
};

struct FoundationExecutionConfiguration2 {
    solvers::NewtonianValidityConfiguration validity{};
    bool contact_enabled{true};
    solvers::FrictionlessContactSystemConfiguration contact{};

    friend bool operator==(const FoundationExecutionConfiguration2&,
                           const FoundationExecutionConfiguration2&) = default;
};

// Forward-compatible, length-delimited registry section. A runtime must reject
// restoration when it does not understand a present section; decoders retain
// the bytes so newer canonical registries do not require a framing change.
struct CheckpointExtensionSectionV2 {
    std::uint32_t type_id{};
    std::uint32_t section_revision{};
    std::string payload;

    friend bool operator==(const CheckpointExtensionSectionV2&, const CheckpointExtensionSectionV2&) = default;
};

// Unlike SnapshotV1, this is a complete foundation resume image. It contains
// every resource sampled by the current particle/gravity solver plus the
// persistent identities needed to continue deterministic allocation. The wire
// representation is canonical little-endian binary; it is not host-layout or
// locale dependent.
struct FoundationCheckpointV2 {
    std::uint32_t schema_version{foundation_checkpoint_schema_version_v2};
    SimulationContractV2 simulation_contract;
    std::uint32_t semantic_seed_mixer_version{core::semantic_seed_mixer_version};
    std::uint64_t world_seed{};
    AllocatorCursorV2 particle_ids;
    AllocatorCursorV2 boundary_ids;
    AllocatorCursorV2 gravity_operator_ids;
    std::uint64_t world_tick{};
    double simulation_time_seconds{};
    std::uint64_t particle_revision{};
    std::vector<ParticleRecordV2> particles;
    std::uint64_t boundary_revision{};
    std::vector<BoundaryRecordV2> boundaries;
    std::uint64_t material_revision{};
    MaterialCatalogV2 material_catalog;
    std::vector<MaterialRecordV2> materials;
    ConstantGravityFieldV2 base_gravity;
    std::uint64_t gravity_operator_revision{};
    GravityCompositionPolicyV2 gravity_composition_policy{GravityCompositionPolicyV2::TransformChain};
    std::vector<GravityOperatorRecordV2> gravity_operators;
    std::uint64_t mechanical_response_revision{};
    std::vector<MechanicalResponseRecordV2> mechanical_responses;
    std::uint64_t disc_collider_revision{};
    std::vector<DiscColliderRecordV2> disc_colliders;
    FoundationExecutionConfigurationRecordV2 execution_configuration;
    std::vector<CheckpointExtensionSectionV2> extension_sections;

    friend bool operator==(const FoundationCheckpointV2&, const FoundationCheckpointV2&) = default;
};

struct FoundationCaptureContextV2 {
    SimulationContractV2 simulation_contract;
    std::uint64_t world_seed{};
    AllocatorCursorV2 particle_ids;
    AllocatorCursorV2 boundary_ids;
    AllocatorCursorV2 gravity_operator_ids;
    ConstantGravityFieldV2 base_gravity;
    FoundationExecutionConfiguration2 execution_configuration;
};

struct RestoredFoundationV2 {
    world::WorldState2 world;
    materials::MaterialRegistry materials;
    ConstantGravityFieldV2 base_gravity;
    operators::GravityOperatorGraph gravity_operators;
    materials::MechanicalResponseRegistry mechanical_responses;
    world::DiscColliderRegistry2 disc_colliders;
    SimulationContractV2 simulation_contract;
    std::uint32_t semantic_seed_mixer_version{};
    std::uint64_t world_seed{};
    AllocatorCursorV2 particle_ids;
    AllocatorCursorV2 boundary_ids;
    AllocatorCursorV2 gravity_operator_ids;
    FoundationExecutionConfiguration2 execution_configuration;
};

struct CheckpointDecodeLimitsV2 {
    std::size_t maximum_input_bytes{64U * 1024U * 1024U};
    std::size_t maximum_string_bytes{1U * 1024U * 1024U};
    std::size_t maximum_particles{1'000'000};
    std::size_t maximum_boundaries{1'000'000};
    std::size_t maximum_boundary_conditions{4'000'000};
    std::size_t maximum_materials{1'000'000};
    std::size_t maximum_catalog_entries{4'000'000};
    std::size_t maximum_gravity_operators{1'000'000};
    std::size_t maximum_mechanical_responses{1'000'000};
    std::size_t maximum_disc_colliders{1'000'000};
    std::size_t maximum_extension_sections{1024};
    std::size_t maximum_extension_bytes{64U * 1024U * 1024U};
};

[[nodiscard]] std::expected<void, CheckpointError> validate_checkpoint_v2(
    const FoundationCheckpointV2& checkpoint);

[[nodiscard]] std::expected<FoundationCheckpointV2, CheckpointError> capture_checkpoint_v2(
    const world::WorldState2& world,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    const world::DiscColliderRegistry2& disc_colliders,
    const operators::GravityOperatorGraph& gravity_operators,
    const FoundationCaptureContextV2& context);

[[nodiscard]] std::expected<std::string, CheckpointError> encode_checkpoint_v2(
    const FoundationCheckpointV2& checkpoint);

[[nodiscard]] std::expected<FoundationCheckpointV2, CheckpointError> decode_checkpoint_v2(
    std::string_view encoded,
    CheckpointDecodeLimitsV2 limits = {});

// Transactional by construction: the caller receives a fully restored bundle
// only after every resource succeeds. The supported contract must come from
// the selected runtime solver; serialization deliberately owns no integrator
// identity and requires an exact match instead of guessing compatibility.
[[nodiscard]] std::expected<RestoredFoundationV2, CheckpointError> restore_checkpoint_v2(
    const FoundationCheckpointV2& checkpoint,
    const SimulationContractV2& supported_contract,
    const FoundationExecutionConfiguration2& supported_execution_configuration);

}  // namespace principia::serialization
