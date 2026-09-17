#include <principia/serialization/checkpoint.hpp>

#include <principia/units/quantity.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

namespace principia::serialization {
namespace {

constexpr std::string_view checkpoint_magic = "principia.foundation.checkpoint";

[[nodiscard]] CheckpointError make_error(
    CheckpointErrorCode code,
    std::string message,
    std::size_t byte_offset = 0)
{
    return CheckpointError{code, 0, byte_offset + 1, std::move(message)};
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

class BinaryWriter {
public:
    void bytes(std::string_view value) { output_.append(value); }

    void u8(std::uint8_t value) { output_.push_back(static_cast<char>(value)); }

    template <std::unsigned_integral Integer>
    void unsigned_integer(Integer value)
    {
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            const auto shift = static_cast<unsigned int>(index * 8U);
            u8(static_cast<std::uint8_t>((value >> shift) & static_cast<Integer>(0xFFU)));
        }
    }

    template <std::signed_integral Integer>
    void signed_integer(Integer value)
    {
        unsigned_integer(std::bit_cast<std::make_unsigned_t<Integer>>(value));
    }

    void boolean(bool value) { u8(value ? std::uint8_t{1} : std::uint8_t{0}); }

    void floating_point(double value)
    {
        unsigned_integer(std::bit_cast<std::uint64_t>(canonical_zero(value)));
    }

    void string(std::string_view value)
    {
        unsigned_integer(static_cast<std::uint64_t>(value.size()));
        bytes(value);
    }

    [[nodiscard]] std::string finish() && { return std::move(output_); }

private:
    std::string output_;
};

class BinaryReader {
public:
    BinaryReader(std::string_view input, const CheckpointDecodeLimitsV2& limits)
        : input_(input), limits_(limits)
    {
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] bool finished() const noexcept { return offset_ == input_.size(); }

    [[nodiscard]] std::expected<std::string_view, CheckpointError> bytes(
        std::size_t count,
        std::string_view description)
    {
        if (count > input_.size() - offset_) {
            return std::unexpected(make_error(
                CheckpointErrorCode::UnexpectedEnd,
                "expected " + std::string(description) + " before end of checkpoint",
                offset_));
        }
        const auto result = input_.substr(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::expected<std::uint8_t, CheckpointError> u8(std::string_view description)
    {
        auto encoded = bytes(1, description);
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        return static_cast<std::uint8_t>(static_cast<unsigned char>((*encoded)[0]));
    }

    template <std::unsigned_integral Integer>
    [[nodiscard]] std::expected<Integer, CheckpointError> unsigned_integer(std::string_view description)
    {
        auto encoded = bytes(sizeof(Integer), description);
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        Integer value{};
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            const auto byte = static_cast<Integer>(
                static_cast<unsigned char>((*encoded)[index]));
            const auto shift = static_cast<unsigned int>(index * 8U);
            value |= static_cast<Integer>(byte << shift);
        }
        return value;
    }

    template <std::signed_integral Integer>
    [[nodiscard]] std::expected<Integer, CheckpointError> signed_integer(std::string_view description)
    {
        auto value = unsigned_integer<std::make_unsigned_t<Integer>>(description);
        if (!value) {
            return std::unexpected(value.error());
        }
        return std::bit_cast<Integer>(*value);
    }

    [[nodiscard]] std::expected<bool, CheckpointError> boolean(std::string_view description)
    {
        const auto value_offset = offset_;
        auto value = u8(description);
        if (!value) {
            return std::unexpected(value.error());
        }
        if (*value > 1U) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                std::string(description) + " must be encoded as zero or one",
                value_offset));
        }
        return *value != 0U;
    }

    [[nodiscard]] std::expected<double, CheckpointError> floating_point(std::string_view description)
    {
        const auto value_offset = offset_;
        auto bits = unsigned_integer<std::uint64_t>(description);
        if (!bits) {
            return std::unexpected(bits.error());
        }
        const auto value = std::bit_cast<double>(*bits);
        if (!std::isfinite(value)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                std::string(description) + " must be finite",
                value_offset));
        }
        return canonical_zero(value);
    }

    [[nodiscard]] std::expected<std::string, CheckpointError> string(std::string_view description)
    {
        const auto length_offset = offset_;
        auto length = unsigned_integer<std::uint64_t>("string length");
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidCount,
                std::string(description) + " length exceeds the platform limit",
                length_offset));
        }
        const auto count = static_cast<std::size_t>(*length);
        if (count > limits_.maximum_string_bytes ||
            string_bytes_ > limits_.maximum_string_bytes - count) {
            return std::unexpected(make_error(
                CheckpointErrorCode::StringLimitExceeded,
                "checkpoint strings exceed the caller-supplied byte limit",
                length_offset));
        }
        auto encoded = bytes(count, description);
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        string_bytes_ += count;
        return std::string{*encoded};
    }

    [[nodiscard]] std::expected<std::string, CheckpointError> blob(
        std::string_view description,
        std::size_t maximum)
    {
        const auto length_offset = offset_;
        auto length = unsigned_integer<std::uint64_t>("blob length");
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            *length > static_cast<std::uint64_t>(maximum)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::ExtensionLimitExceeded,
                std::string(description) + " exceeds the caller-supplied byte limit",
                length_offset));
        }
        auto encoded = bytes(static_cast<std::size_t>(*length), description);
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        return std::string{*encoded};
    }

    [[nodiscard]] std::expected<std::size_t, CheckpointError> count(
        std::string_view description,
        std::size_t maximum,
        CheckpointErrorCode limit_code)
    {
        const auto count_offset = offset_;
        auto value = unsigned_integer<std::uint64_t>(description);
        if (!value) {
            return std::unexpected(value.error());
        }
        if (*value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidCount,
                std::string(description) + " exceeds the platform record limit",
                count_offset));
        }
        if (*value > static_cast<std::uint64_t>(maximum)) {
            return std::unexpected(make_error(
                limit_code,
                std::string(description) + " exceeds the caller-supplied decode limit",
                count_offset));
        }
        return static_cast<std::size_t>(*value);
    }

private:
    std::string_view input_;
    const CheckpointDecodeLimitsV2& limits_;
    std::size_t offset_{};
    std::size_t string_bytes_{};
};

template <typename Enum>
[[nodiscard]] constexpr bool one_of(Enum value, std::initializer_list<Enum> values) noexcept
{
    return std::ranges::find(values, value) != values.end();
}

[[nodiscard]] bool valid_numeric_abi(const NumericAbiV2& abi) noexcept
{
    return !abi.id.empty() && abi.revision != 0U &&
           one_of(abi.scalar_format, {ScalarFormatV2::Ieee754Binary64}) &&
           one_of(abi.byte_order, {ByteOrderV2::LittleEndian}) &&
           one_of(abi.quantity_encoding, {QuantityEncodingV2::CanonicalSiScalars}) &&
           one_of(
               abi.determinism,
               {DeterminismGuaranteeV2::BitwiseWithinBuild,
                DeterminismGuaranteeV2::DeterministicWithinTolerance}) &&
           one_of(
               abi.floating_point,
               {FloatingPointPolicyV2::StrictIeee754,
                FloatingPointPolicyV2::DeclaredContraction}) &&
           one_of(abi.non_finite, {NonFinitePolicyV2::RejectInputAndProposal}) &&
           one_of(abi.iteration_order, {IterationOrderPolicyV2::CanonicalPersistentId}) &&
           one_of(
               abi.accumulation,
               {AccumulationPolicyV2::SerialCanonical,
                AccumulationPolicyV2::CanonicalPairwiseTree});
}

[[nodiscard]] bool valid_contract(const SimulationContractV2& contract) noexcept
{
    return contract.theory_id != 0U && !contract.integrator_id.empty() &&
           contract.integrator_revision != 0U && contract.validity_contract_revision != 0U &&
           contract.step_contract_revision != 0U && contract.fixed_frame_id != 0U &&
           std::isfinite(contract.fixed_step_seconds) && contract.fixed_step_seconds > 0.0 &&
           contract.world_topology_dimension != 0U && contract.physical_field_dimension != 0U &&
           contract.spacetime_model_dimension != 0U && valid_numeric_abi(contract.numeric_abi);
}

[[nodiscard]] std::expected<void, CheckpointError> validate_execution_configuration(
    const FoundationExecutionConfigurationRecordV2& configuration)
{
    if (!std::isfinite(configuration.invariant_speed_metres_per_second) ||
        !std::isfinite(configuration.maximum_beta) ||
        !std::isfinite(configuration.contact_geometric_tolerance_metres) ||
        !std::isfinite(configuration.contact_simultaneous_time_tolerance_seconds)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::NonFiniteValue,
            "execution-configuration scalar values must be finite"));
    }
    if (configuration.invariant_speed_metres_per_second <= 0.0 ||
        configuration.maximum_beta <= 0.0 ||
        configuration.maximum_contact_bodies == 0U ||
        configuration.maximum_contact_boundaries == 0U ||
        configuration.contact_geometric_tolerance_metres < 0.0 ||
        configuration.contact_simultaneous_time_tolerance_seconds < 0.0) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidExecutionConfiguration,
            "execution configuration contains an invalid validity or contact value"));
    }
    if (configuration.maximum_contact_bodies >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        configuration.maximum_contact_boundaries >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidCount,
            "execution-configuration contact budget exceeds the platform size limit"));
    }
    return {};
}

[[nodiscard]] std::expected<FoundationExecutionConfigurationRecordV2, CheckpointError>
capture_execution_configuration(const FoundationExecutionConfiguration2& configuration)
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (configuration.contact.maximum_bodies > std::numeric_limits<std::uint64_t>::max() ||
            configuration.contact.maximum_boundaries > std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidCount,
                "native contact budget cannot be represented by the V2 fixed-width wire format"));
        }
    }
    return FoundationExecutionConfigurationRecordV2{
        configuration.contact_enabled,
        units::in_metres_per_second(configuration.validity.invariant_speed),
        configuration.validity.maximum_beta,
        configuration.contact.maximum_events,
        static_cast<std::uint64_t>(configuration.contact.maximum_bodies),
        static_cast<std::uint64_t>(configuration.contact.maximum_boundaries),
        configuration.contact.geometric_tolerance_metres,
        configuration.contact.simultaneous_time_tolerance_seconds,
    };
}

[[nodiscard]] std::expected<FoundationExecutionConfiguration2, CheckpointError>
restore_execution_configuration(const FoundationExecutionConfigurationRecordV2& configuration)
{
    if (auto validity = validate_execution_configuration(configuration); !validity) {
        return std::unexpected(validity.error());
    }
    return FoundationExecutionConfiguration2{
        solvers::NewtonianValidityConfiguration{
            units::metres_per_second(configuration.invariant_speed_metres_per_second),
            configuration.maximum_beta,
        },
        configuration.contact_enabled,
        solvers::FrictionlessContactSystemConfiguration{
            configuration.maximum_contact_events,
            static_cast<std::size_t>(configuration.maximum_contact_bodies),
            static_cast<std::size_t>(configuration.maximum_contact_boundaries),
            configuration.contact_geometric_tolerance_metres,
            configuration.contact_simultaneous_time_tolerance_seconds,
        },
    };
}

[[nodiscard]] bool valid_constraint(ParticleConstraintV2 constraint) noexcept
{
    return one_of(constraint, {ParticleConstraintV2::Free, ParticleConstraintV2::Fixed});
}

[[nodiscard]] bool valid_boundary_condition(BoundaryConditionV2 condition) noexcept
{
    return one_of(
        condition,
        {BoundaryConditionV2::MechanicallyRigid,
         BoundaryConditionV2::ThermallyConductive,
         BoundaryConditionV2::ThermallyInsulating,
         BoundaryConditionV2::ElectricallyConductive,
         BoundaryConditionV2::ElectricallyInsulating,
         BoundaryConditionV2::OpticallyTransparent,
         BoundaryConditionV2::FluidImpermeable});
}

[[nodiscard]] bool valid_composition_policy(GravityCompositionPolicyV2 policy) noexcept
{
    return one_of(
        policy,
        {GravityCompositionPolicyV2::Additive,
         GravityCompositionPolicyV2::Multiplicative,
         GravityCompositionPolicyV2::ReplaceByPriority,
         GravityCompositionPolicyV2::TransformChain,
         GravityCompositionPolicyV2::ConstraintResolved});
}

[[nodiscard]] bool valid_impact_response(ImpactResponseKindV2 kind) noexcept
{
    return one_of(kind, {ImpactResponseKindV2::FrictionlessRestitution});
}

[[nodiscard]] bool transform_allowed(
    GravityCompositionPolicyV2 policy,
    const GravityTransformV2& transform) noexcept
{
    switch (policy) {
    case GravityCompositionPolicyV2::Additive:
        return std::holds_alternative<OffsetGravityV2>(transform);
    case GravityCompositionPolicyV2::Multiplicative:
        return std::holds_alternative<ScaleGravityV2>(transform);
    case GravityCompositionPolicyV2::ReplaceByPriority:
        return true;
    case GravityCompositionPolicyV2::TransformChain:
        return std::holds_alternative<RotateGravityV2>(transform) ||
               std::holds_alternative<ScaleGravityV2>(transform);
    case GravityCompositionPolicyV2::ConstraintResolved:
        return false;
    }
    return false;
}

void canonicalize(FoundationCheckpointV2& checkpoint)
{
    checkpoint.simulation_contract.fixed_step_seconds =
        canonical_zero(checkpoint.simulation_contract.fixed_step_seconds);
    checkpoint.simulation_time_seconds = canonical_zero(checkpoint.simulation_time_seconds);
    checkpoint.execution_configuration.invariant_speed_metres_per_second =
        canonical_zero(checkpoint.execution_configuration.invariant_speed_metres_per_second);
    checkpoint.execution_configuration.maximum_beta =
        canonical_zero(checkpoint.execution_configuration.maximum_beta);
    checkpoint.execution_configuration.contact_geometric_tolerance_metres =
        canonical_zero(checkpoint.execution_configuration.contact_geometric_tolerance_metres);
    checkpoint.execution_configuration.contact_simultaneous_time_tolerance_seconds = canonical_zero(
        checkpoint.execution_configuration.contact_simultaneous_time_tolerance_seconds);

    for (auto& particle : checkpoint.particles) {
        particle.position_x_metres = canonical_zero(particle.position_x_metres);
        particle.position_y_metres = canonical_zero(particle.position_y_metres);
        particle.momentum_x_kilogram_metres_per_second =
            canonical_zero(particle.momentum_x_kilogram_metres_per_second);
        particle.momentum_y_kilogram_metres_per_second =
            canonical_zero(particle.momentum_y_kilogram_metres_per_second);
        particle.rest_mass_kilograms = canonical_zero(particle.rest_mass_kilograms);
    }
    std::ranges::sort(checkpoint.particles, {}, &ParticleRecordV2::id);

    for (auto& boundary : checkpoint.boundaries) {
        boundary.minimum_x_metres = canonical_zero(boundary.minimum_x_metres);
        boundary.minimum_y_metres = canonical_zero(boundary.minimum_y_metres);
        boundary.maximum_x_metres = canonical_zero(boundary.maximum_x_metres);
        boundary.maximum_y_metres = canonical_zero(boundary.maximum_y_metres);
        std::ranges::sort(boundary.conditions, {}, &BoundaryConditionRecordV2::channel_id);
    }
    std::ranges::sort(checkpoint.boundaries, {}, &BoundaryRecordV2::id);
    std::ranges::sort(checkpoint.materials, {}, &MaterialRecordV2::id);

    auto canonicalize_family = [](CatalogFamilyV2& family) {
        std::ranges::sort(family.identifiers);
    };
    canonicalize_family(checkpoint.material_catalog.compositions);
    canonicalize_family(checkpoint.material_catalog.mechanical_models);
    canonicalize_family(checkpoint.material_catalog.thermal_models);
    canonicalize_family(checkpoint.material_catalog.electrical_models);
    canonicalize_family(checkpoint.material_catalog.magnetic_models);
    canonicalize_family(checkpoint.material_catalog.optical_models);
    canonicalize_family(checkpoint.material_catalog.phase_models);

    checkpoint.base_gravity.x_metres_per_second_squared =
        canonical_zero(checkpoint.base_gravity.x_metres_per_second_squared);
    checkpoint.base_gravity.y_metres_per_second_squared =
        canonical_zero(checkpoint.base_gravity.y_metres_per_second_squared);

    for (auto& node : checkpoint.gravity_operators) {
        node.support_center_x_metres = canonical_zero(node.support_center_x_metres);
        node.support_center_y_metres = canonical_zero(node.support_center_y_metres);
        node.support_radius_metres = canonical_zero(node.support_radius_metres);
        std::visit(
            [](auto& transform) {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV2>) {
                    transform.counterclockwise_quarter_turns =
                        canonical_quarter_turns(transform.counterclockwise_quarter_turns);
                } else if constexpr (std::same_as<Transform, ScaleGravityV2>) {
                    transform.dimensionless_factor = canonical_zero(transform.dimensionless_factor);
                } else {
                    transform.x_metres_per_second_squared =
                        canonical_zero(transform.x_metres_per_second_squared);
                    transform.y_metres_per_second_squared =
                        canonical_zero(transform.y_metres_per_second_squared);
                }
            },
            node.transform);
    }
    std::ranges::sort(checkpoint.gravity_operators, [](const auto& left, const auto& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        return left.id < right.id;
    });
    for (auto& response : checkpoint.mechanical_responses) {
        response.normal_coefficient_of_restitution =
            canonical_zero(response.normal_coefficient_of_restitution);
    }
    std::ranges::sort(checkpoint.mechanical_responses, {}, &MechanicalResponseRecordV2::id);
    for (auto& collider : checkpoint.disc_colliders) {
        collider.radius_metres = canonical_zero(collider.radius_metres);
    }
    std::ranges::sort(checkpoint.disc_colliders, {}, &DiscColliderRecordV2::particle_id);
    std::ranges::sort(checkpoint.extension_sections, {}, &CheckpointExtensionSectionV2::type_id);
}

template <typename Id>
[[nodiscard]] std::vector<std::uint32_t> capture_catalog_family(const std::optional<std::set<Id>>& family)
{
    std::vector<std::uint32_t> result;
    if (!family) {
        return result;
    }
    result.reserve(family->size());
    for (const auto id : *family) {
        result.push_back(id.value());
    }
    return result;
}

template <typename Id>
[[nodiscard]] CatalogFamilyV2 capture_catalog_family_record(const std::optional<std::set<Id>>& family)
{
    return CatalogFamilyV2{family.has_value(), capture_catalog_family(family)};
}

template <typename Id>
[[nodiscard]] std::optional<std::set<Id>> restore_catalog_family(const CatalogFamilyV2& family)
{
    if (!family.known) {
        return std::nullopt;
    }
    std::set<Id> result;
    for (const auto value : family.identifiers) {
        result.emplace(value);
    }
    return result;
}

[[nodiscard]] std::optional<materials::MaterialModelCatalog> restore_catalog(
    const MaterialCatalogV2& catalog)
{
    if (!catalog.present) {
        return std::nullopt;
    }
    return materials::MaterialModelCatalog{
        restore_catalog_family<materials::CompositionId>(catalog.compositions),
        restore_catalog_family<materials::MechanicalModelId>(catalog.mechanical_models),
        restore_catalog_family<materials::ThermalModelId>(catalog.thermal_models),
        restore_catalog_family<materials::ElectricalModelId>(catalog.electrical_models),
        restore_catalog_family<materials::MagneticModelId>(catalog.magnetic_models),
        restore_catalog_family<materials::OpticalModelId>(catalog.optical_models),
        restore_catalog_family<materials::PhaseModelId>(catalog.phase_models),
    };
}

[[nodiscard]] std::expected<boundaries::BoundaryConditionKind, CheckpointError> restore_condition(
    BoundaryConditionV2 condition)
{
    switch (condition) {
    case BoundaryConditionV2::MechanicallyRigid:
        return boundaries::BoundaryConditionKind::MechanicallyRigid;
    case BoundaryConditionV2::ThermallyConductive:
        return boundaries::BoundaryConditionKind::ThermallyConductive;
    case BoundaryConditionV2::ThermallyInsulating:
        return boundaries::BoundaryConditionKind::ThermallyInsulating;
    case BoundaryConditionV2::ElectricallyConductive:
        return boundaries::BoundaryConditionKind::ElectricallyConductive;
    case BoundaryConditionV2::ElectricallyInsulating:
        return boundaries::BoundaryConditionKind::ElectricallyInsulating;
    case BoundaryConditionV2::OpticallyTransparent:
        return boundaries::BoundaryConditionKind::OpticallyTransparent;
    case BoundaryConditionV2::FluidImpermeable:
        return boundaries::BoundaryConditionKind::FluidImpermeable;
    }
    return std::unexpected(make_error(CheckpointErrorCode::InvalidEnum, "unknown boundary condition"));
}

[[nodiscard]] BoundaryConditionV2 capture_condition(boundaries::BoundaryConditionKind condition)
{
    switch (condition) {
    case boundaries::BoundaryConditionKind::MechanicallyRigid:
        return BoundaryConditionV2::MechanicallyRigid;
    case boundaries::BoundaryConditionKind::ThermallyConductive:
        return BoundaryConditionV2::ThermallyConductive;
    case boundaries::BoundaryConditionKind::ThermallyInsulating:
        return BoundaryConditionV2::ThermallyInsulating;
    case boundaries::BoundaryConditionKind::ElectricallyConductive:
        return BoundaryConditionV2::ElectricallyConductive;
    case boundaries::BoundaryConditionKind::ElectricallyInsulating:
        return BoundaryConditionV2::ElectricallyInsulating;
    case boundaries::BoundaryConditionKind::OpticallyTransparent:
        return BoundaryConditionV2::OpticallyTransparent;
    case boundaries::BoundaryConditionKind::FluidImpermeable:
        return BoundaryConditionV2::FluidImpermeable;
    }
    return static_cast<BoundaryConditionV2>(std::numeric_limits<std::uint8_t>::max());
}

[[nodiscard]] std::expected<operators::CompositionPolicy, CheckpointError> restore_policy(
    GravityCompositionPolicyV2 policy)
{
    switch (policy) {
    case GravityCompositionPolicyV2::Additive:
        return operators::CompositionPolicy::Additive;
    case GravityCompositionPolicyV2::Multiplicative:
        return operators::CompositionPolicy::Multiplicative;
    case GravityCompositionPolicyV2::ReplaceByPriority:
        return operators::CompositionPolicy::ReplaceByPriority;
    case GravityCompositionPolicyV2::TransformChain:
        return operators::CompositionPolicy::TransformChain;
    case GravityCompositionPolicyV2::ConstraintResolved:
        return operators::CompositionPolicy::ConstraintResolved;
    }
    return std::unexpected(make_error(CheckpointErrorCode::InvalidEnum, "unknown gravity policy"));
}

[[nodiscard]] GravityCompositionPolicyV2 capture_policy(operators::CompositionPolicy policy)
{
    switch (policy) {
    case operators::CompositionPolicy::Additive:
        return GravityCompositionPolicyV2::Additive;
    case operators::CompositionPolicy::Multiplicative:
        return GravityCompositionPolicyV2::Multiplicative;
    case operators::CompositionPolicy::ReplaceByPriority:
        return GravityCompositionPolicyV2::ReplaceByPriority;
    case operators::CompositionPolicy::TransformChain:
        return GravityCompositionPolicyV2::TransformChain;
    case operators::CompositionPolicy::ConstraintResolved:
        return GravityCompositionPolicyV2::ConstraintResolved;
    }
    return static_cast<GravityCompositionPolicyV2>(std::numeric_limits<std::uint8_t>::max());
}

[[nodiscard]] GravityTransformV2 capture_transform(const operators::GravityTransform& transform)
{
    return std::visit(
        [](const auto& operation) -> GravityTransformV2 {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, operators::RotateGravityQuarterTurns>) {
                return RotateGravityV2{operation.counterclockwise_quarter_turns};
            } else if constexpr (std::same_as<Operation, operators::ScaleGravity>) {
                return ScaleGravityV2{operation.dimensionless_factor};
            } else {
                return OffsetGravityV2{
                    units::in_metres_per_second_squared(operation.offset[0]),
                    units::in_metres_per_second_squared(operation.offset[1]),
                };
            }
        },
        transform);
}

[[nodiscard]] operators::GravityTransform restore_transform(const GravityTransformV2& transform)
{
    return std::visit(
        [](const auto& operation) -> operators::GravityTransform {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, RotateGravityV2>) {
                return operators::RotateGravityQuarterTurns{operation.counterclockwise_quarter_turns};
            } else if constexpr (std::same_as<Operation, ScaleGravityV2>) {
                return operators::ScaleGravity{operation.dimensionless_factor};
            } else {
                return operators::OffsetGravity{fields::GravityVector<2>{
                    units::metres_per_second_squared(operation.x_metres_per_second_squared),
                    units::metres_per_second_squared(operation.y_metres_per_second_squared),
                }};
            }
        },
        transform);
}

[[nodiscard]] std::expected<void, CheckpointError> validate_family(
    const CatalogFamilyV2& family,
    std::string_view name)
{
    if (!family.known && !family.identifiers.empty()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            std::string(name) + " catalog entries cannot exist when the family is unknown"));
    }
    auto identifiers = family.identifiers;
    if (std::ranges::find(identifiers, 0U) != identifiers.end()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidIdentifier,
            std::string(name) + " catalog IDs must be nonzero"));
    }
    std::ranges::sort(identifiers);
    if (std::ranges::adjacent_find(identifiers) != identifiers.end()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::DuplicateIdentifier,
            std::string(name) + " catalog IDs must be unique"));
    }
    return {};
}

[[nodiscard]] std::expected<void, CheckpointError> validate_cursor(
    const AllocatorCursorV2& cursor,
    std::uint64_t maximum_live_id,
    std::string_view name)
{
    if (cursor.exhausted) {
        if (cursor.next_identifier != 0U) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidAllocatorState,
                std::string(name) + " exhausted cursor must use the zero sentinel"));
        }
        return {};
    }
    if (cursor.next_identifier == 0U || cursor.next_identifier <= maximum_live_id) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidAllocatorState,
            std::string(name) + " next ID must be nonzero and greater than every live ID"));
    }
    return {};
}

[[nodiscard]] std::expected<void, CheckpointError> require_supported_contract(
    const FoundationCheckpointV2& checkpoint,
    const SimulationContractV2& supported_contract)
{
    const auto& contract = checkpoint.simulation_contract;
    if (checkpoint.semantic_seed_mixer_version != core::semantic_seed_mixer_version) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedSeedMixerVersion,
            "checkpoint semantic seed mixer version is not supported by this runtime"));
    }
    if (!valid_contract(supported_contract)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidSimulationContract,
            "runtime-supported simulation contract is incomplete"));
    }
    if (contract != supported_contract) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedSimulationContract,
            "checkpoint execution contract does not exactly match the runtime-supported contract"));
    }
    return {};
}

[[nodiscard]] std::expected<void, CheckpointError> require_supported_execution_configuration(
    const FoundationCheckpointV2& checkpoint,
    const FoundationExecutionConfiguration2& supported_configuration)
{
    auto supported_record = capture_execution_configuration(supported_configuration);
    if (!supported_record) {
        return std::unexpected(supported_record.error());
    }
    if (auto validity = validate_execution_configuration(*supported_record); !validity) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidExecutionConfiguration,
            "runtime-supported execution configuration is invalid"));
    }
    if (checkpoint.execution_configuration != *supported_record) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedExecutionConfiguration,
            "checkpoint execution configuration does not exactly match the caller-selected runtime configuration"));
    }
    return {};
}

void write_catalog_family(BinaryWriter& writer, const CatalogFamilyV2& family)
{
    writer.boolean(family.known);
    writer.unsigned_integer(static_cast<std::uint64_t>(family.identifiers.size()));
    for (const auto identifier : family.identifiers) {
        writer.unsigned_integer(identifier);
    }
}

[[nodiscard]] std::expected<CatalogFamilyV2, CheckpointError> read_catalog_family(
    BinaryReader& reader,
    std::size_t& catalog_entries,
    const CheckpointDecodeLimitsV2& limits)
{
    auto known = reader.boolean("catalog family availability");
    if (!known) {
        return std::unexpected(known.error());
    }
    const auto remaining = limits.maximum_catalog_entries - catalog_entries;
    auto count = reader.count(
        "catalog family entry count",
        remaining,
        CheckpointErrorCode::CatalogEntryLimitExceeded);
    if (!count) {
        return std::unexpected(count.error());
    }
    catalog_entries += *count;
    CatalogFamilyV2 family;
    family.known = *known;
    family.identifiers.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index) {
        static_cast<void>(index);
        auto identifier = reader.unsigned_integer<std::uint32_t>("catalog identifier");
        if (!identifier) {
            return std::unexpected(identifier.error());
        }
        family.identifiers.push_back(*identifier);
    }
    return family;
}

[[nodiscard]] std::string encode_mechanical_response_section(const FoundationCheckpointV2& checkpoint)
{
    BinaryWriter writer;
    writer.unsigned_integer(checkpoint.mechanical_response_revision);
    writer.unsigned_integer(static_cast<std::uint64_t>(checkpoint.mechanical_responses.size()));
    for (const auto& response : checkpoint.mechanical_responses) {
        writer.unsigned_integer(response.id);
        writer.string(response.name);
        writer.u8(static_cast<std::uint8_t>(response.impact_kind));
        writer.floating_point(response.normal_coefficient_of_restitution);
    }
    return std::move(writer).finish();
}

[[nodiscard]] std::string encode_disc_collider_section(const FoundationCheckpointV2& checkpoint)
{
    BinaryWriter writer;
    writer.unsigned_integer(checkpoint.disc_collider_revision);
    writer.unsigned_integer(static_cast<std::uint64_t>(checkpoint.disc_colliders.size()));
    for (const auto& collider : checkpoint.disc_colliders) {
        writer.unsigned_integer(collider.particle_id);
        writer.floating_point(collider.radius_metres);
    }
    return std::move(writer).finish();
}

[[nodiscard]] std::string encode_execution_configuration_section(
    const FoundationCheckpointV2& checkpoint)
{
    const auto& configuration = checkpoint.execution_configuration;
    BinaryWriter writer;
    writer.boolean(configuration.contact_enabled);
    writer.floating_point(configuration.invariant_speed_metres_per_second);
    writer.floating_point(configuration.maximum_beta);
    writer.unsigned_integer(configuration.maximum_contact_events);
    writer.unsigned_integer(configuration.maximum_contact_bodies);
    writer.unsigned_integer(configuration.maximum_contact_boundaries);
    writer.floating_point(configuration.contact_geometric_tolerance_metres);
    writer.floating_point(configuration.contact_simultaneous_time_tolerance_seconds);
    return std::move(writer).finish();
}

[[nodiscard]] std::expected<void, CheckpointError> decode_mechanical_response_section(
    std::string_view payload,
    FoundationCheckpointV2& checkpoint,
    const CheckpointDecodeLimitsV2& limits)
{
    BinaryReader reader{payload, limits};
    auto revision = reader.unsigned_integer<std::uint64_t>("mechanical-response revision");
    if (!revision) {
        return std::unexpected(revision.error());
    }
    checkpoint.mechanical_response_revision = *revision;
    auto count = reader.count(
        "mechanical-response count",
        limits.maximum_mechanical_responses,
        CheckpointErrorCode::MechanicalResponseLimitExceeded);
    if (!count) {
        return std::unexpected(count.error());
    }
    checkpoint.mechanical_responses.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index) {
        static_cast<void>(index);
        MechanicalResponseRecordV2 response;
        auto id = reader.unsigned_integer<std::uint32_t>("mechanical-response ID");
        if (!id) {
            return std::unexpected(id.error());
        }
        response.id = *id;
        auto name = reader.string("mechanical-response name");
        if (!name) {
            return std::unexpected(name.error());
        }
        response.name = std::move(*name);
        auto kind = reader.u8("impact-response kind");
        if (!kind) {
            return std::unexpected(kind.error());
        }
        response.impact_kind = static_cast<ImpactResponseKindV2>(*kind);
        auto restitution = reader.floating_point("normal coefficient of restitution");
        if (!restitution) {
            return std::unexpected(restitution.error());
        }
        response.normal_coefficient_of_restitution = *restitution;
        checkpoint.mechanical_responses.push_back(std::move(response));
    }
    if (!reader.finished()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::TrailingData,
            "unexpected bytes in mechanical-response registry section",
            reader.offset()));
    }
    return {};
}

[[nodiscard]] std::expected<void, CheckpointError> decode_disc_collider_section(
    std::string_view payload,
    FoundationCheckpointV2& checkpoint,
    const CheckpointDecodeLimitsV2& limits)
{
    BinaryReader reader{payload, limits};
    auto revision = reader.unsigned_integer<std::uint64_t>("disc-collider revision");
    if (!revision) {
        return std::unexpected(revision.error());
    }
    checkpoint.disc_collider_revision = *revision;
    auto count = reader.count(
        "disc-collider count",
        limits.maximum_disc_colliders,
        CheckpointErrorCode::ColliderLimitExceeded);
    if (!count) {
        return std::unexpected(count.error());
    }
    checkpoint.disc_colliders.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index) {
        static_cast<void>(index);
        DiscColliderRecordV2 collider;
        auto particle = reader.unsigned_integer<std::uint64_t>("collider particle ID");
        if (!particle) {
            return std::unexpected(particle.error());
        }
        collider.particle_id = *particle;
        auto radius = reader.floating_point("collider radius");
        if (!radius) {
            return std::unexpected(radius.error());
        }
        collider.radius_metres = *radius;
        checkpoint.disc_colliders.push_back(collider);
    }
    if (!reader.finished()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::TrailingData,
            "unexpected bytes in disc-collider registry section",
            reader.offset()));
    }
    return {};
}


[[nodiscard]] std::expected<void, CheckpointError> decode_execution_configuration_section(
    std::string_view payload,
    FoundationCheckpointV2& checkpoint,
    const CheckpointDecodeLimitsV2& limits)
{
    BinaryReader reader{payload, limits};
    auto& configuration = checkpoint.execution_configuration;
    auto contact_enabled = reader.boolean("contact-enabled flag");
    if (!contact_enabled) {
        return std::unexpected(contact_enabled.error());
    }
    configuration.contact_enabled = *contact_enabled;
    auto invariant_speed = reader.floating_point("Newtonian invariant speed");
    if (!invariant_speed) {
        return std::unexpected(invariant_speed.error());
    }
    configuration.invariant_speed_metres_per_second = *invariant_speed;
    auto maximum_beta = reader.floating_point("Newtonian maximum beta");
    if (!maximum_beta) {
        return std::unexpected(maximum_beta.error());
    }
    configuration.maximum_beta = *maximum_beta;
    auto maximum_events = reader.unsigned_integer<std::uint32_t>("maximum contact events");
    if (!maximum_events) {
        return std::unexpected(maximum_events.error());
    }
    configuration.maximum_contact_events = *maximum_events;
    auto maximum_bodies = reader.unsigned_integer<std::uint64_t>("maximum contact bodies");
    if (!maximum_bodies) {
        return std::unexpected(maximum_bodies.error());
    }
    configuration.maximum_contact_bodies = *maximum_bodies;
    auto maximum_boundaries = reader.unsigned_integer<std::uint64_t>("maximum contact boundaries");
    if (!maximum_boundaries) {
        return std::unexpected(maximum_boundaries.error());
    }
    configuration.maximum_contact_boundaries = *maximum_boundaries;
    auto geometric_tolerance = reader.floating_point("contact geometric tolerance");
    if (!geometric_tolerance) {
        return std::unexpected(geometric_tolerance.error());
    }
    configuration.contact_geometric_tolerance_metres = *geometric_tolerance;
    auto simultaneous_tolerance = reader.floating_point("contact simultaneous-time tolerance");
    if (!simultaneous_tolerance) {
        return std::unexpected(simultaneous_tolerance.error());
    }
    configuration.contact_simultaneous_time_tolerance_seconds = *simultaneous_tolerance;
    if (!reader.finished()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::TrailingData,
            "unexpected bytes in execution-configuration section",
            reader.offset()));
    }
    return {};
}

}  // namespace

std::expected<void, CheckpointError> validate_checkpoint_v2(const FoundationCheckpointV2& checkpoint)
{
    if (checkpoint.schema_version != foundation_checkpoint_schema_version_v2) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedSchemaVersion,
            "foundation checkpoint schema version is unsupported"));
    }
    if (!valid_contract(checkpoint.simulation_contract)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidSimulationContract,
            "simulation contract is incomplete or contains an invalid enum/value"));
    }
    if (auto validity = validate_execution_configuration(checkpoint.execution_configuration); !validity) {
        return validity;
    }
    if (checkpoint.semantic_seed_mixer_version == 0U) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidSimulationContract,
            "semantic seed mixer version must be nonzero"));
    }
    if (!std::isfinite(checkpoint.simulation_time_seconds)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::NonFiniteValue,
            "simulation time must be finite"));
    }
    if (checkpoint.simulation_time_seconds < 0.0) {
        return std::unexpected(make_error(
            CheckpointErrorCode::NegativeSimulationTime,
            "simulation time must not be negative"));
    }
    if (checkpoint.particle_revision == std::numeric_limits<std::uint64_t>::max() ||
        checkpoint.boundary_revision == std::numeric_limits<std::uint64_t>::max() ||
        checkpoint.material_revision == std::numeric_limits<std::uint64_t>::max() ||
        checkpoint.gravity_operator_revision == std::numeric_limits<std::uint64_t>::max() ||
        checkpoint.mechanical_response_revision == std::numeric_limits<std::uint64_t>::max() ||
        checkpoint.disc_collider_revision == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidRevision,
            "resource revisions must retain one value for the next committed mutation"));
    }
    if (checkpoint.particle_revision < checkpoint.particles.size() ||
        checkpoint.boundary_revision < checkpoint.boundaries.size() ||
        checkpoint.material_revision != checkpoint.materials.size() ||
        checkpoint.gravity_operator_revision < checkpoint.gravity_operators.size() ||
        checkpoint.mechanical_response_revision != checkpoint.mechanical_responses.size() ||
        checkpoint.disc_collider_revision < checkpoint.disc_colliders.size() ||
        ((checkpoint.gravity_operator_revision - checkpoint.gravity_operators.size()) % 2U) != 0U ||
        ((checkpoint.disc_collider_revision - checkpoint.disc_colliders.size()) % 2U) != 0U) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidRevision,
            "resource revision is inconsistent with its live record count"));
    }

    std::set<std::uint32_t> material_ids;
    for (const auto& material : checkpoint.materials) {
        if (material.id == 0U || material.composition_id == 0U || material.mechanical_model_id == 0U ||
            material.thermal_model_id == 0U || material.electrical_model_id == 0U ||
            material.magnetic_model_id == 0U || material.optical_model_id == 0U ||
            material.phase_model_id == 0U) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "material and constitutive-model IDs must be nonzero"));
        }
        if (material.name.empty()) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "material names must not be empty"));
        }
        if (!material_ids.insert(material.id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "material IDs must be unique"));
        }
    }

    const CatalogFamilyV2* const families[]{
        &checkpoint.material_catalog.compositions,
        &checkpoint.material_catalog.mechanical_models,
        &checkpoint.material_catalog.thermal_models,
        &checkpoint.material_catalog.electrical_models,
        &checkpoint.material_catalog.magnetic_models,
        &checkpoint.material_catalog.optical_models,
        &checkpoint.material_catalog.phase_models,
    };
    constexpr std::string_view family_names[]{
        "composition",
        "mechanical",
        "thermal",
        "electrical",
        "magnetic",
        "optical",
        "phase",
    };
    for (std::size_t index = 0; index < std::size(families); ++index) {
        if (!checkpoint.material_catalog.present &&
            (families[index]->known || !families[index]->identifiers.empty())) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "absent material catalog cannot contain family metadata"));
        }
        if (auto validity = validate_family(*families[index], family_names[index]); !validity) {
            return validity;
        }
    }

    std::set<std::uint32_t> mechanical_response_ids;
    for (const auto& response : checkpoint.mechanical_responses) {
        if (response.id == 0U || response.name.empty() || !valid_impact_response(response.impact_kind)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "mechanical responses require a nonzero ID, name, and known impact law"));
        }
        if (!mechanical_response_ids.insert(response.id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "mechanical-response IDs must be unique"));
        }
        if (!std::isfinite(response.normal_coefficient_of_restitution)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                "mechanical restitution coefficients must be finite"));
        }
        if (response.normal_coefficient_of_restitution < 0.0 ||
            response.normal_coefficient_of_restitution > 1.0) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "mechanical restitution coefficients must lie in the closed unit interval"));
        }
        if (checkpoint.material_catalog.mechanical_models.known &&
            std::ranges::find(
                checkpoint.material_catalog.mechanical_models.identifiers,
                response.id) == checkpoint.material_catalog.mechanical_models.identifiers.end()) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "mechanical response is absent from the known mechanical-model catalog"));
        }
    }

    auto runtime_catalog = restore_catalog(checkpoint.material_catalog);
    for (const auto& material : checkpoint.materials) {
        const materials::MaterialDefinition definition{
            materials::MaterialId{material.id},
            material.name,
            materials::CompositionId{material.composition_id},
            materials::MechanicalModelId{material.mechanical_model_id},
            materials::ThermalModelId{material.thermal_model_id},
            materials::ElectricalModelId{material.electrical_model_id},
            materials::MagneticModelId{material.magnetic_model_id},
            materials::OpticalModelId{material.optical_model_id},
            materials::PhaseModelId{material.phase_model_id},
        };
        if (!materials::validate_material_definition(
                definition,
                runtime_catalog ? &*runtime_catalog : nullptr)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "material definition violates its catalog foreign-key contract"));
        }
        if (!mechanical_response_ids.contains(material.mechanical_model_id)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "material mechanical model has no resumable response definition"));
        }
    }

    std::set<std::uint64_t> particle_ids;
    std::map<std::uint64_t, ParticleConstraintV2> particle_constraints;
    std::uint64_t maximum_particle_id{};
    for (const auto& particle : checkpoint.particles) {
        if (particle.id == 0U || particle.material_id == 0U) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "particle and material IDs must be nonzero"));
        }
        if (!particle_ids.insert(particle.id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "particle IDs must be unique"));
        }
        particle_constraints.emplace(particle.id, particle.constraint);
        maximum_particle_id = std::max(maximum_particle_id, particle.id);
        if (!std::isfinite(particle.position_x_metres) || !std::isfinite(particle.position_y_metres) ||
            !std::isfinite(particle.momentum_x_kilogram_metres_per_second) ||
            !std::isfinite(particle.momentum_y_kilogram_metres_per_second) ||
            !std::isfinite(particle.rest_mass_kilograms)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                "particle state must be finite"));
        }
        if (particle.rest_mass_kilograms <= 0.0) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonPositiveMass,
                "particle rest mass must be positive"));
        }
        if (!valid_constraint(particle.constraint)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                "particle constraint is unknown"));
        }
        if (particle.constraint == ParticleConstraintV2::Fixed &&
            (particle.momentum_x_kilogram_metres_per_second != 0.0 ||
             particle.momentum_y_kilogram_metres_per_second != 0.0)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::FixedParticleHasMomentum,
                "fixed particles must have zero canonical momentum"));
        }
        if (!material_ids.contains(particle.material_id)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::UnresolvedMaterial,
                "particle material ID is absent from the checkpoint material registry"));
        }
    }

    std::set<std::uint64_t> collider_particles;
    for (const auto& collider : checkpoint.disc_colliders) {
        if (collider.particle_id == 0U || !particle_ids.contains(collider.particle_id)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "disc collider must reference a particle present in the checkpoint"));
        }
        if (!collider_particles.insert(collider.particle_id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "a particle may have at most one disc collider"));
        }
        if (!std::isfinite(collider.radius_metres)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                "disc collider radius must be finite"));
        }
        if (collider.radius_metres <= 0.0) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "disc collider radius must be positive"));
        }
    }
    if (!checkpoint.execution_configuration.contact_enabled && !checkpoint.disc_colliders.empty()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidExecutionConfiguration,
            "disc colliders require contact execution to be enabled"));
    }
    if (checkpoint.execution_configuration.contact_enabled) {
        if (checkpoint.disc_colliders.size() >
                checkpoint.execution_configuration.maximum_contact_bodies ||
            checkpoint.boundaries.size() >
                checkpoint.execution_configuration.maximum_contact_boundaries) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidExecutionConfiguration,
                "enabled contact resource counts exceed the persisted execution budgets"));
        }
        for (const auto& collider : checkpoint.disc_colliders) {
            if (particle_constraints.at(collider.particle_id) != ParticleConstraintV2::Free) {
                return std::unexpected(make_error(
                    CheckpointErrorCode::InvalidExecutionConfiguration,
                    "enabled contact requires every collider particle to be free"));
            }
        }
    }

    std::set<std::uint64_t> boundary_ids;
    std::uint64_t maximum_boundary_id{};
    for (const auto& boundary : checkpoint.boundaries) {
        if (boundary.id == 0U || boundary.name.empty()) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "boundary ID and name must be present"));
        }
        if (!boundary_ids.insert(boundary.id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "boundary IDs must be unique"));
        }
        maximum_boundary_id = std::max(maximum_boundary_id, boundary.id);
        if (!std::isfinite(boundary.minimum_x_metres) || !std::isfinite(boundary.minimum_y_metres) ||
            !std::isfinite(boundary.maximum_x_metres) || !std::isfinite(boundary.maximum_y_metres)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                "boundary extents must be finite"));
        }
        if (boundary.minimum_x_metres > boundary.maximum_x_metres ||
            boundary.minimum_y_metres > boundary.maximum_y_metres) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "boundary extents must not be inverted"));
        }
        std::set<std::uint32_t> condition_channels;
        boundaries::BoundaryDefinition definition{
            boundaries::BoundaryId{boundary.id},
            boundary.name,
            boundaries::AxisAlignedBox2{
                spacetime::WorldPosition<2>{
                    units::metres(boundary.minimum_x_metres),
                    units::metres(boundary.minimum_y_metres)},
                spacetime::WorldPosition<2>{
                    units::metres(boundary.maximum_x_metres),
                    units::metres(boundary.maximum_y_metres)},
            },
            {},
        };
        for (const auto& condition : boundary.conditions) {
            if (condition.channel_id == 0U || !valid_boundary_condition(condition.condition) ||
                !condition_channels.insert(condition.channel_id).second) {
                return std::unexpected(make_error(
                    CheckpointErrorCode::InvalidDefinition,
                    "boundary conditions require unique nonzero channels and known condition kinds"));
            }
            auto runtime_condition = restore_condition(condition.condition);
            if (!runtime_condition) {
                return std::unexpected(runtime_condition.error());
            }
            definition.conditions.emplace(state::StateChannelId{condition.channel_id}, *runtime_condition);
        }
        if (!boundaries::validate_boundary_definition(definition)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "boundary definition contains an incompatible channel condition"));
        }
    }

    if (checkpoint.base_gravity.field_type_id != fields::effective_newtonian_gravity_field_id.value() ||
        checkpoint.base_gravity.name.empty() ||
        checkpoint.base_gravity.field_location !=
            static_cast<std::uint8_t>(state::FieldLocation::CellCentered) ||
        checkpoint.base_gravity.storage_kind !=
            static_cast<std::uint8_t>(fields::FieldStorageKind::Constant) ||
        checkpoint.base_gravity.physical_components != 2U) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidGravityField,
            "base gravity must be the supported two-component constant effective field"));
    }
    if (!std::isfinite(checkpoint.base_gravity.x_metres_per_second_squared) ||
        !std::isfinite(checkpoint.base_gravity.y_metres_per_second_squared)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::NonFiniteValue,
            "base gravity components must be finite"));
    }

    if (!valid_composition_policy(checkpoint.gravity_composition_policy)) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidEnum,
            "gravity composition policy is unknown"));
    }
    std::set<std::uint64_t> operator_ids;
    std::uint64_t maximum_operator_id{};
    for (const auto& node : checkpoint.gravity_operators) {
        if (node.id == 0U || !operator_ids.insert(node.id).second) {
            return std::unexpected(make_error(
                node.id == 0U ? CheckpointErrorCode::InvalidIdentifier
                              : CheckpointErrorCode::DuplicateIdentifier,
                "gravity operator IDs must be nonzero and unique"));
        }
        maximum_operator_id = std::max(maximum_operator_id, node.id);
        if (!std::isfinite(node.support_center_x_metres) ||
            !std::isfinite(node.support_center_y_metres) ||
            !std::isfinite(node.support_radius_metres) || node.support_radius_metres < 0.0) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidDefinition,
                "gravity support must be finite with a nonnegative radius"));
        }
        const auto finite_transform = std::visit(
            [](const auto& transform) {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV2>) {
                    return true;
                } else if constexpr (std::same_as<Transform, ScaleGravityV2>) {
                    return std::isfinite(transform.dimensionless_factor);
                } else {
                    return std::isfinite(transform.x_metres_per_second_squared) &&
                           std::isfinite(transform.y_metres_per_second_squared);
                }
            },
            node.transform);
        if (!finite_transform) {
            return std::unexpected(make_error(
                CheckpointErrorCode::NonFiniteValue,
                "gravity transform values must be finite"));
        }
        if (!transform_allowed(checkpoint.gravity_composition_policy, node.transform)) {
            return std::unexpected(make_error(
                CheckpointErrorCode::IncompatibleGravityTransform,
                "gravity transform is incompatible with its composition policy"));
        }
    }

    if (auto validity = validate_cursor(checkpoint.particle_ids, maximum_particle_id, "particle allocator");
        !validity) {
        return validity;
    }
    if (auto validity = validate_cursor(checkpoint.boundary_ids, maximum_boundary_id, "boundary allocator");
        !validity) {
        return validity;
    }
    if (auto validity =
            validate_cursor(checkpoint.gravity_operator_ids, maximum_operator_id, "operator allocator");
        !validity) {
        return validity;
    }
    std::set<std::uint32_t> extension_ids;
    for (const auto& section : checkpoint.extension_sections) {
        if (section.type_id == 0U || section.section_revision == 0U) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "extension section type and revision IDs must be nonzero"));
        }
        if (!extension_ids.insert(section.type_id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "extension section type IDs must be unique"));
        }
        if (section.type_id == mechanical_response_section_type_v2 ||
            section.type_id == disc_collider_section_type_v2 ||
            section.type_id == foundation_execution_configuration_section_type_v2) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "opaque extension section collides with a typed foundation registry section"));
        }
    }
    return {};
}

std::expected<FoundationCheckpointV2, CheckpointError> capture_checkpoint_v2(
    const world::WorldState2& world,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    const world::DiscColliderRegistry2& disc_colliders,
    const operators::GravityOperatorGraph& gravity_operators,
    const FoundationCaptureContextV2& context)
{
    FoundationCheckpointV2 checkpoint;
    checkpoint.simulation_contract = context.simulation_contract;
    checkpoint.semantic_seed_mixer_version = core::semantic_seed_mixer_version;
    checkpoint.world_seed = context.world_seed;
    checkpoint.particle_ids = context.particle_ids;
    checkpoint.boundary_ids = context.boundary_ids;
    checkpoint.gravity_operator_ids = context.gravity_operator_ids;
    auto execution_configuration = capture_execution_configuration(context.execution_configuration);
    if (!execution_configuration) {
        return std::unexpected(execution_configuration.error());
    }
    checkpoint.execution_configuration = *execution_configuration;
    checkpoint.world_tick = world.tick;
    checkpoint.simulation_time_seconds = units::in_seconds(world.simulation_time.elapsed());
    checkpoint.particle_revision = world.particles.revision();
    checkpoint.boundary_revision = world.boundaries.revision();
    checkpoint.material_revision = materials.revision();
    checkpoint.mechanical_response_revision = mechanical_responses.revision();
    checkpoint.disc_collider_revision = disc_colliders.revision();
    checkpoint.base_gravity = context.base_gravity;
    checkpoint.gravity_operator_revision = gravity_operators.revision();
    checkpoint.gravity_composition_policy = capture_policy(gravity_operators.composition_policy());

    checkpoint.particles.reserve(world.particles.ordered_particles().size());
    for (const auto& [id, particle] : world.particles.ordered_particles()) {
        if (id != particle.id) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "particle store key does not match its persistent ID"));
        }
        ParticleConstraintV2 constraint{};
        switch (particle.constraint) {
        case world::KinematicConstraint::Free:
            constraint = ParticleConstraintV2::Free;
            break;
        case world::KinematicConstraint::Fixed:
            constraint = ParticleConstraintV2::Fixed;
            break;
        default:
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                "runtime particle constraint is unknown"));
        }
        checkpoint.particles.push_back(ParticleRecordV2{
            id.value(),
            units::in_metres(particle.position[0]),
            units::in_metres(particle.position[1]),
            units::in_kilogram_metres_per_second(particle.momentum[0]),
            units::in_kilogram_metres_per_second(particle.momentum[1]),
            units::in_kilograms(particle.rest_mass),
            particle.material.value(),
            constraint,
        });
    }

    checkpoint.boundaries.reserve(world.boundaries.size());
    for (const auto& [id, boundary] : world.boundaries.ordered_definitions()) {
        if (id != boundary.id) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "boundary registry key does not match its persistent ID"));
        }
        BoundaryRecordV2 record{
            id.value(),
            boundary.name,
            units::in_metres(boundary.extent.minimum[0]),
            units::in_metres(boundary.extent.minimum[1]),
            units::in_metres(boundary.extent.maximum[0]),
            units::in_metres(boundary.extent.maximum[1]),
            {},
        };
        record.conditions.reserve(boundary.conditions.size());
        for (const auto& [channel, condition] : boundary.conditions) {
            record.conditions.push_back(BoundaryConditionRecordV2{
                channel.value(),
                capture_condition(condition),
            });
        }
        checkpoint.boundaries.push_back(std::move(record));
    }

    if (const auto* catalog = materials.model_catalog(); catalog != nullptr) {
        checkpoint.material_catalog.present = true;
        checkpoint.material_catalog.compositions = capture_catalog_family_record(catalog->compositions);
        checkpoint.material_catalog.mechanical_models = capture_catalog_family_record(catalog->mechanical_models);
        checkpoint.material_catalog.thermal_models = capture_catalog_family_record(catalog->thermal_models);
        checkpoint.material_catalog.electrical_models = capture_catalog_family_record(catalog->electrical_models);
        checkpoint.material_catalog.magnetic_models = capture_catalog_family_record(catalog->magnetic_models);
        checkpoint.material_catalog.optical_models = capture_catalog_family_record(catalog->optical_models);
        checkpoint.material_catalog.phase_models = capture_catalog_family_record(catalog->phase_models);
    }
    checkpoint.materials.reserve(materials.size());
    for (const auto& [id, material] : materials.ordered_definitions()) {
        if (id != material.id) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "material registry key does not match its persistent ID"));
        }
        checkpoint.materials.push_back(MaterialRecordV2{
            id.value(),
            material.name,
            material.composition.value(),
            material.mechanical.value(),
            material.thermal.value(),
            material.electrical.value(),
            material.magnetic.value(),
            material.optical.value(),
            material.phase.value(),
        });
    }

    checkpoint.mechanical_responses.reserve(mechanical_responses.size());
    for (const auto& [id, response] : mechanical_responses.ordered_definitions()) {
        if (id != response.id) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "mechanical-response registry key does not match its persistent ID"));
        }
        ImpactResponseKindV2 impact_kind{};
        switch (response.impact_kind) {
        case materials::ImpactResponseKind::FrictionlessRestitution:
            impact_kind = ImpactResponseKindV2::FrictionlessRestitution;
            break;
        default:
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                "runtime impact-response kind is unknown"));
        }
        checkpoint.mechanical_responses.push_back(MechanicalResponseRecordV2{
            id.value(),
            response.name,
            impact_kind,
            response.normal_coefficient_of_restitution,
        });
    }

    checkpoint.disc_colliders.reserve(disc_colliders.size());
    for (const auto& [particle, collider] : disc_colliders.ordered_colliders()) {
        if (particle != collider.particle) {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidIdentifier,
                "disc-collider registry key does not match its particle ID"));
        }
        checkpoint.disc_colliders.push_back(DiscColliderRecordV2{
            particle.value(),
            units::in_metres(collider.radius),
        });
    }

    checkpoint.gravity_operators.reserve(gravity_operators.ordered_nodes().size());
    for (const auto& node : gravity_operators.ordered_nodes()) {
        checkpoint.gravity_operators.push_back(GravityOperatorRecordV2{
            node.id.value(),
            node.priority,
            units::in_metres(node.support.center[0]),
            units::in_metres(node.support.center[1]),
            units::in_metres(node.support.radius),
            capture_transform(node.transform),
        });
    }

    canonicalize(checkpoint);
    if (auto validity = validate_checkpoint_v2(checkpoint); !validity) {
        return std::unexpected(validity.error());
    }
    return checkpoint;
}

std::expected<std::string, CheckpointError> encode_checkpoint_v2(
    const FoundationCheckpointV2& checkpoint)
{
    auto canonical = checkpoint;
    canonicalize(canonical);
    if (auto validity = validate_checkpoint_v2(canonical); !validity) {
        return std::unexpected(validity.error());
    }

    BinaryWriter writer;
    writer.bytes(checkpoint_magic);
    writer.unsigned_integer(canonical.schema_version);

    const auto& contract = canonical.simulation_contract;
    writer.unsigned_integer(contract.theory_id);
    writer.string(contract.integrator_id);
    writer.unsigned_integer(contract.integrator_revision);
    writer.unsigned_integer(contract.validity_contract_revision);
    writer.unsigned_integer(contract.step_contract_revision);
    writer.unsigned_integer(contract.fixed_frame_id);
    writer.floating_point(contract.fixed_step_seconds);
    writer.unsigned_integer(contract.world_topology_dimension);
    writer.unsigned_integer(contract.physical_field_dimension);
    writer.unsigned_integer(contract.spacetime_model_dimension);
    writer.string(contract.numeric_abi.id);
    writer.unsigned_integer(contract.numeric_abi.revision);
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.scalar_format));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.byte_order));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.quantity_encoding));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.determinism));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.floating_point));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.non_finite));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.iteration_order));
    writer.u8(static_cast<std::uint8_t>(contract.numeric_abi.accumulation));

    writer.unsigned_integer(canonical.semantic_seed_mixer_version);
    writer.unsigned_integer(canonical.world_seed);
    const AllocatorCursorV2* const cursors[]{
        &canonical.particle_ids,
        &canonical.boundary_ids,
        &canonical.gravity_operator_ids,
    };
    for (const auto* cursor : cursors) {
        writer.unsigned_integer(cursor->next_identifier);
        writer.boolean(cursor->exhausted);
    }

    writer.unsigned_integer(canonical.world_tick);
    writer.floating_point(canonical.simulation_time_seconds);
    writer.unsigned_integer(canonical.particle_revision);
    writer.unsigned_integer(static_cast<std::uint64_t>(canonical.particles.size()));
    for (const auto& particle : canonical.particles) {
        writer.unsigned_integer(particle.id);
        writer.floating_point(particle.position_x_metres);
        writer.floating_point(particle.position_y_metres);
        writer.floating_point(particle.momentum_x_kilogram_metres_per_second);
        writer.floating_point(particle.momentum_y_kilogram_metres_per_second);
        writer.floating_point(particle.rest_mass_kilograms);
        writer.unsigned_integer(particle.material_id);
        writer.u8(static_cast<std::uint8_t>(particle.constraint));
    }

    writer.unsigned_integer(canonical.boundary_revision);
    writer.unsigned_integer(static_cast<std::uint64_t>(canonical.boundaries.size()));
    for (const auto& boundary : canonical.boundaries) {
        writer.unsigned_integer(boundary.id);
        writer.string(boundary.name);
        writer.floating_point(boundary.minimum_x_metres);
        writer.floating_point(boundary.minimum_y_metres);
        writer.floating_point(boundary.maximum_x_metres);
        writer.floating_point(boundary.maximum_y_metres);
        writer.unsigned_integer(static_cast<std::uint64_t>(boundary.conditions.size()));
        for (const auto& condition : boundary.conditions) {
            writer.unsigned_integer(condition.channel_id);
            writer.u8(static_cast<std::uint8_t>(condition.condition));
        }
    }

    writer.unsigned_integer(canonical.material_revision);
    writer.boolean(canonical.material_catalog.present);
    write_catalog_family(writer, canonical.material_catalog.compositions);
    write_catalog_family(writer, canonical.material_catalog.mechanical_models);
    write_catalog_family(writer, canonical.material_catalog.thermal_models);
    write_catalog_family(writer, canonical.material_catalog.electrical_models);
    write_catalog_family(writer, canonical.material_catalog.magnetic_models);
    write_catalog_family(writer, canonical.material_catalog.optical_models);
    write_catalog_family(writer, canonical.material_catalog.phase_models);
    writer.unsigned_integer(static_cast<std::uint64_t>(canonical.materials.size()));
    for (const auto& material : canonical.materials) {
        writer.unsigned_integer(material.id);
        writer.string(material.name);
        writer.unsigned_integer(material.composition_id);
        writer.unsigned_integer(material.mechanical_model_id);
        writer.unsigned_integer(material.thermal_model_id);
        writer.unsigned_integer(material.electrical_model_id);
        writer.unsigned_integer(material.magnetic_model_id);
        writer.unsigned_integer(material.optical_model_id);
        writer.unsigned_integer(material.phase_model_id);
    }

    writer.unsigned_integer(canonical.base_gravity.field_type_id);
    writer.string(canonical.base_gravity.name);
    writer.u8(canonical.base_gravity.field_location);
    writer.u8(canonical.base_gravity.storage_kind);
    writer.u8(canonical.base_gravity.physical_components);
    writer.floating_point(canonical.base_gravity.x_metres_per_second_squared);
    writer.floating_point(canonical.base_gravity.y_metres_per_second_squared);

    writer.unsigned_integer(canonical.gravity_operator_revision);
    writer.u8(static_cast<std::uint8_t>(canonical.gravity_composition_policy));
    writer.unsigned_integer(static_cast<std::uint64_t>(canonical.gravity_operators.size()));
    for (const auto& node : canonical.gravity_operators) {
        writer.unsigned_integer(node.id);
        writer.signed_integer(node.priority);
        writer.floating_point(node.support_center_x_metres);
        writer.floating_point(node.support_center_y_metres);
        writer.floating_point(node.support_radius_metres);
        std::visit(
            [&writer](const auto& transform) {
                using Transform = std::remove_cvref_t<decltype(transform)>;
                if constexpr (std::same_as<Transform, RotateGravityV2>) {
                    writer.u8(0U);
                    writer.signed_integer(transform.counterclockwise_quarter_turns);
                } else if constexpr (std::same_as<Transform, ScaleGravityV2>) {
                    writer.u8(1U);
                    writer.floating_point(transform.dimensionless_factor);
                } else {
                    writer.u8(2U);
                    writer.floating_point(transform.x_metres_per_second_squared);
                    writer.floating_point(transform.y_metres_per_second_squared);
                }
            },
            node.transform);
    }
    auto sections = canonical.extension_sections;
    sections.push_back(CheckpointExtensionSectionV2{
        mechanical_response_section_type_v2,
        foundation_registry_section_revision_v2,
        encode_mechanical_response_section(canonical),
    });
    sections.push_back(CheckpointExtensionSectionV2{
        disc_collider_section_type_v2,
        foundation_registry_section_revision_v2,
        encode_disc_collider_section(canonical),
    });
    sections.push_back(CheckpointExtensionSectionV2{
        foundation_execution_configuration_section_type_v2,
        foundation_execution_configuration_section_revision_v2,
        encode_execution_configuration_section(canonical),
    });
    std::ranges::sort(sections, {}, &CheckpointExtensionSectionV2::type_id);
    writer.unsigned_integer(static_cast<std::uint64_t>(sections.size()));
    for (const auto& section : sections) {
        writer.unsigned_integer(section.type_id);
        writer.unsigned_integer(section.section_revision);
        writer.string(section.payload);
    }
    return std::move(writer).finish();
}

std::expected<FoundationCheckpointV2, CheckpointError> decode_checkpoint_v2(
    std::string_view encoded,
    CheckpointDecodeLimitsV2 limits)
{
    if (encoded.size() > limits.maximum_input_bytes) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InputLimitExceeded,
            "checkpoint input exceeds the caller-supplied byte limit"));
    }
    BinaryReader reader{encoded, limits};
    auto magic = reader.bytes(checkpoint_magic.size(), "checkpoint header");
    if (!magic) {
        return std::unexpected(magic.error());
    }
    if (*magic != checkpoint_magic) {
        return std::unexpected(make_error(CheckpointErrorCode::InvalidHeader, "invalid checkpoint header"));
    }
    auto version = reader.unsigned_integer<std::uint32_t>("schema version");
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version != foundation_checkpoint_schema_version_v2) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedSchemaVersion,
            "unsupported foundation checkpoint schema version"));
    }

    FoundationCheckpointV2 checkpoint;
    checkpoint.schema_version = *version;
    auto& contract = checkpoint.simulation_contract;
#define PRINCIPIA_READ_ASSIGN(target, expression)       \
    do {                                                 \
        auto decoded_value = (expression);               \
        if (!decoded_value) {                            \
            return std::unexpected(decoded_value.error()); \
        }                                                \
        (target) = std::move(*decoded_value);             \
    } while (false)

    PRINCIPIA_READ_ASSIGN(contract.theory_id, reader.unsigned_integer<std::uint32_t>("theory ID"));
    PRINCIPIA_READ_ASSIGN(contract.integrator_id, reader.string("integrator ID"));
    PRINCIPIA_READ_ASSIGN(
        contract.integrator_revision,
        reader.unsigned_integer<std::uint32_t>("integrator revision"));
    PRINCIPIA_READ_ASSIGN(
        contract.validity_contract_revision,
        reader.unsigned_integer<std::uint32_t>("validity contract revision"));
    PRINCIPIA_READ_ASSIGN(
        contract.step_contract_revision,
        reader.unsigned_integer<std::uint32_t>("step contract revision"));
    PRINCIPIA_READ_ASSIGN(contract.fixed_frame_id, reader.unsigned_integer<std::uint32_t>("fixed frame ID"));
    PRINCIPIA_READ_ASSIGN(contract.fixed_step_seconds, reader.floating_point("fixed step"));
    PRINCIPIA_READ_ASSIGN(
        contract.world_topology_dimension,
        reader.unsigned_integer<std::uint32_t>("world topology dimension"));
    PRINCIPIA_READ_ASSIGN(
        contract.physical_field_dimension,
        reader.unsigned_integer<std::uint32_t>("physical field dimension"));
    PRINCIPIA_READ_ASSIGN(
        contract.spacetime_model_dimension,
        reader.unsigned_integer<std::uint32_t>("spacetime model dimension"));
    PRINCIPIA_READ_ASSIGN(contract.numeric_abi.id, reader.string("numeric ABI ID"));
    PRINCIPIA_READ_ASSIGN(
        contract.numeric_abi.revision,
        reader.unsigned_integer<std::uint32_t>("numeric ABI revision"));
    std::uint8_t enum_value{};
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("scalar format"));
    contract.numeric_abi.scalar_format = static_cast<ScalarFormatV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("byte order"));
    contract.numeric_abi.byte_order = static_cast<ByteOrderV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("quantity encoding"));
    contract.numeric_abi.quantity_encoding = static_cast<QuantityEncodingV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("determinism guarantee"));
    contract.numeric_abi.determinism = static_cast<DeterminismGuaranteeV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("floating-point policy"));
    contract.numeric_abi.floating_point = static_cast<FloatingPointPolicyV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("non-finite policy"));
    contract.numeric_abi.non_finite = static_cast<NonFinitePolicyV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("iteration-order policy"));
    contract.numeric_abi.iteration_order = static_cast<IterationOrderPolicyV2>(enum_value);
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("accumulation policy"));
    contract.numeric_abi.accumulation = static_cast<AccumulationPolicyV2>(enum_value);

    PRINCIPIA_READ_ASSIGN(
        checkpoint.semantic_seed_mixer_version,
        reader.unsigned_integer<std::uint32_t>("semantic seed mixer version"));
    PRINCIPIA_READ_ASSIGN(checkpoint.world_seed, reader.unsigned_integer<std::uint64_t>("world seed"));
    AllocatorCursorV2* const cursors[]{
        &checkpoint.particle_ids,
        &checkpoint.boundary_ids,
        &checkpoint.gravity_operator_ids,
    };
    for (auto* cursor : cursors) {
        PRINCIPIA_READ_ASSIGN(
            cursor->next_identifier,
            reader.unsigned_integer<std::uint64_t>("allocator next ID"));
        PRINCIPIA_READ_ASSIGN(cursor->exhausted, reader.boolean("allocator exhaustion flag"));
    }

    PRINCIPIA_READ_ASSIGN(checkpoint.world_tick, reader.unsigned_integer<std::uint64_t>("world tick"));
    PRINCIPIA_READ_ASSIGN(checkpoint.simulation_time_seconds, reader.floating_point("simulation time"));
    PRINCIPIA_READ_ASSIGN(
        checkpoint.particle_revision,
        reader.unsigned_integer<std::uint64_t>("particle revision"));
    auto particle_count = reader.count(
        "particle count",
        limits.maximum_particles,
        CheckpointErrorCode::ParticleLimitExceeded);
    if (!particle_count) {
        return std::unexpected(particle_count.error());
    }
    checkpoint.particles.reserve(*particle_count);
    for (std::size_t index = 0; index < *particle_count; ++index) {
        static_cast<void>(index);
        ParticleRecordV2 particle;
        PRINCIPIA_READ_ASSIGN(particle.id, reader.unsigned_integer<std::uint64_t>("particle ID"));
        PRINCIPIA_READ_ASSIGN(particle.position_x_metres, reader.floating_point("particle x position"));
        PRINCIPIA_READ_ASSIGN(particle.position_y_metres, reader.floating_point("particle y position"));
        PRINCIPIA_READ_ASSIGN(
            particle.momentum_x_kilogram_metres_per_second,
            reader.floating_point("particle x momentum"));
        PRINCIPIA_READ_ASSIGN(
            particle.momentum_y_kilogram_metres_per_second,
            reader.floating_point("particle y momentum"));
        PRINCIPIA_READ_ASSIGN(particle.rest_mass_kilograms, reader.floating_point("particle rest mass"));
        PRINCIPIA_READ_ASSIGN(particle.material_id, reader.unsigned_integer<std::uint32_t>("material ID"));
        PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("particle constraint"));
        particle.constraint = static_cast<ParticleConstraintV2>(enum_value);
        checkpoint.particles.push_back(std::move(particle));
    }

    PRINCIPIA_READ_ASSIGN(
        checkpoint.boundary_revision,
        reader.unsigned_integer<std::uint64_t>("boundary revision"));
    auto boundary_count = reader.count(
        "boundary count",
        limits.maximum_boundaries,
        CheckpointErrorCode::BoundaryLimitExceeded);
    if (!boundary_count) {
        return std::unexpected(boundary_count.error());
    }
    checkpoint.boundaries.reserve(*boundary_count);
    std::size_t condition_count_total{};
    for (std::size_t index = 0; index < *boundary_count; ++index) {
        static_cast<void>(index);
        BoundaryRecordV2 boundary;
        PRINCIPIA_READ_ASSIGN(boundary.id, reader.unsigned_integer<std::uint64_t>("boundary ID"));
        PRINCIPIA_READ_ASSIGN(boundary.name, reader.string("boundary name"));
        PRINCIPIA_READ_ASSIGN(boundary.minimum_x_metres, reader.floating_point("boundary minimum x"));
        PRINCIPIA_READ_ASSIGN(boundary.minimum_y_metres, reader.floating_point("boundary minimum y"));
        PRINCIPIA_READ_ASSIGN(boundary.maximum_x_metres, reader.floating_point("boundary maximum x"));
        PRINCIPIA_READ_ASSIGN(boundary.maximum_y_metres, reader.floating_point("boundary maximum y"));
        const auto remaining_conditions = limits.maximum_boundary_conditions - condition_count_total;
        auto condition_count = reader.count(
            "boundary condition count",
            remaining_conditions,
            CheckpointErrorCode::BoundaryConditionLimitExceeded);
        if (!condition_count) {
            return std::unexpected(condition_count.error());
        }
        condition_count_total += *condition_count;
        boundary.conditions.reserve(*condition_count);
        for (std::size_t condition_index = 0; condition_index < *condition_count; ++condition_index) {
            static_cast<void>(condition_index);
            BoundaryConditionRecordV2 condition;
            PRINCIPIA_READ_ASSIGN(
                condition.channel_id,
                reader.unsigned_integer<std::uint32_t>("boundary channel ID"));
            PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("boundary condition"));
            condition.condition = static_cast<BoundaryConditionV2>(enum_value);
            boundary.conditions.push_back(condition);
        }
        checkpoint.boundaries.push_back(std::move(boundary));
    }

    PRINCIPIA_READ_ASSIGN(
        checkpoint.material_revision,
        reader.unsigned_integer<std::uint64_t>("material revision"));
    PRINCIPIA_READ_ASSIGN(
        checkpoint.material_catalog.present,
        reader.boolean("material catalog availability"));
    std::size_t catalog_entries{};
    auto compositions = read_catalog_family(reader, catalog_entries, limits);
    if (!compositions) {
        return std::unexpected(compositions.error());
    }
    checkpoint.material_catalog.compositions = std::move(*compositions);
    auto mechanical = read_catalog_family(reader, catalog_entries, limits);
    if (!mechanical) {
        return std::unexpected(mechanical.error());
    }
    checkpoint.material_catalog.mechanical_models = std::move(*mechanical);
    auto thermal = read_catalog_family(reader, catalog_entries, limits);
    if (!thermal) {
        return std::unexpected(thermal.error());
    }
    checkpoint.material_catalog.thermal_models = std::move(*thermal);
    auto electrical = read_catalog_family(reader, catalog_entries, limits);
    if (!electrical) {
        return std::unexpected(electrical.error());
    }
    checkpoint.material_catalog.electrical_models = std::move(*electrical);
    auto magnetic = read_catalog_family(reader, catalog_entries, limits);
    if (!magnetic) {
        return std::unexpected(magnetic.error());
    }
    checkpoint.material_catalog.magnetic_models = std::move(*magnetic);
    auto optical = read_catalog_family(reader, catalog_entries, limits);
    if (!optical) {
        return std::unexpected(optical.error());
    }
    checkpoint.material_catalog.optical_models = std::move(*optical);
    auto phase = read_catalog_family(reader, catalog_entries, limits);
    if (!phase) {
        return std::unexpected(phase.error());
    }
    checkpoint.material_catalog.phase_models = std::move(*phase);

    auto material_count = reader.count(
        "material count",
        limits.maximum_materials,
        CheckpointErrorCode::MaterialLimitExceeded);
    if (!material_count) {
        return std::unexpected(material_count.error());
    }
    checkpoint.materials.reserve(*material_count);
    for (std::size_t index = 0; index < *material_count; ++index) {
        static_cast<void>(index);
        MaterialRecordV2 material;
        PRINCIPIA_READ_ASSIGN(material.id, reader.unsigned_integer<std::uint32_t>("material ID"));
        PRINCIPIA_READ_ASSIGN(material.name, reader.string("material name"));
        PRINCIPIA_READ_ASSIGN(
            material.composition_id,
            reader.unsigned_integer<std::uint32_t>("composition ID"));
        PRINCIPIA_READ_ASSIGN(
            material.mechanical_model_id,
            reader.unsigned_integer<std::uint32_t>("mechanical model ID"));
        PRINCIPIA_READ_ASSIGN(
            material.thermal_model_id,
            reader.unsigned_integer<std::uint32_t>("thermal model ID"));
        PRINCIPIA_READ_ASSIGN(
            material.electrical_model_id,
            reader.unsigned_integer<std::uint32_t>("electrical model ID"));
        PRINCIPIA_READ_ASSIGN(
            material.magnetic_model_id,
            reader.unsigned_integer<std::uint32_t>("magnetic model ID"));
        PRINCIPIA_READ_ASSIGN(
            material.optical_model_id,
            reader.unsigned_integer<std::uint32_t>("optical model ID"));
        PRINCIPIA_READ_ASSIGN(
            material.phase_model_id,
            reader.unsigned_integer<std::uint32_t>("phase model ID"));
        checkpoint.materials.push_back(std::move(material));
    }

    PRINCIPIA_READ_ASSIGN(
        checkpoint.base_gravity.field_type_id,
        reader.unsigned_integer<std::uint32_t>("gravity field type ID"));
    PRINCIPIA_READ_ASSIGN(checkpoint.base_gravity.name, reader.string("gravity field name"));
    PRINCIPIA_READ_ASSIGN(checkpoint.base_gravity.field_location, reader.u8("gravity field location"));
    PRINCIPIA_READ_ASSIGN(checkpoint.base_gravity.storage_kind, reader.u8("gravity storage kind"));
    PRINCIPIA_READ_ASSIGN(
        checkpoint.base_gravity.physical_components,
        reader.u8("gravity physical component count"));
    PRINCIPIA_READ_ASSIGN(
        checkpoint.base_gravity.x_metres_per_second_squared,
        reader.floating_point("base gravity x component"));
    PRINCIPIA_READ_ASSIGN(
        checkpoint.base_gravity.y_metres_per_second_squared,
        reader.floating_point("base gravity y component"));

    PRINCIPIA_READ_ASSIGN(
        checkpoint.gravity_operator_revision,
        reader.unsigned_integer<std::uint64_t>("gravity operator revision"));
    PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("gravity composition policy"));
    checkpoint.gravity_composition_policy = static_cast<GravityCompositionPolicyV2>(enum_value);
    auto operator_count = reader.count(
        "gravity operator count",
        limits.maximum_gravity_operators,
        CheckpointErrorCode::GravityOperatorLimitExceeded);
    if (!operator_count) {
        return std::unexpected(operator_count.error());
    }
    checkpoint.gravity_operators.reserve(*operator_count);
    for (std::size_t index = 0; index < *operator_count; ++index) {
        static_cast<void>(index);
        GravityOperatorRecordV2 node;
        PRINCIPIA_READ_ASSIGN(node.id, reader.unsigned_integer<std::uint64_t>("gravity operator ID"));
        PRINCIPIA_READ_ASSIGN(node.priority, reader.signed_integer<std::int32_t>("gravity priority"));
        PRINCIPIA_READ_ASSIGN(
            node.support_center_x_metres,
            reader.floating_point("gravity support center x"));
        PRINCIPIA_READ_ASSIGN(
            node.support_center_y_metres,
            reader.floating_point("gravity support center y"));
        PRINCIPIA_READ_ASSIGN(
            node.support_radius_metres,
            reader.floating_point("gravity support radius"));
        PRINCIPIA_READ_ASSIGN(enum_value, reader.u8("gravity transform kind"));
        if (enum_value == 0U) {
            std::int8_t turns{};
            PRINCIPIA_READ_ASSIGN(turns, reader.signed_integer<std::int8_t>("gravity quarter turns"));
            node.transform = RotateGravityV2{turns};
        } else if (enum_value == 1U) {
            double factor{};
            PRINCIPIA_READ_ASSIGN(factor, reader.floating_point("gravity scale"));
            node.transform = ScaleGravityV2{factor};
        } else if (enum_value == 2U) {
            double x{};
            double y{};
            PRINCIPIA_READ_ASSIGN(x, reader.floating_point("gravity offset x"));
            PRINCIPIA_READ_ASSIGN(y, reader.floating_point("gravity offset y"));
            node.transform = OffsetGravityV2{x, y};
        } else {
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                "unknown gravity transform kind",
                reader.offset() - 1U));
        }
        checkpoint.gravity_operators.push_back(std::move(node));
    }
    auto extension_count = reader.count(
        "extension section count",
        limits.maximum_extension_sections,
        CheckpointErrorCode::ExtensionLimitExceeded);
    if (!extension_count) {
        return std::unexpected(extension_count.error());
    }
    checkpoint.extension_sections.reserve(*extension_count);
    std::size_t extension_bytes{};
    bool found_mechanical_responses = false;
    bool found_disc_colliders = false;
    bool found_execution_configuration = false;
    std::set<std::uint32_t> section_ids;
    for (std::size_t index = 0; index < *extension_count; ++index) {
        static_cast<void>(index);
        CheckpointExtensionSectionV2 section;
        PRINCIPIA_READ_ASSIGN(
            section.type_id,
            reader.unsigned_integer<std::uint32_t>("extension section type ID"));
        PRINCIPIA_READ_ASSIGN(
            section.section_revision,
            reader.unsigned_integer<std::uint32_t>("extension section revision"));
        const auto remaining_extension_bytes = limits.maximum_extension_bytes - extension_bytes;
        PRINCIPIA_READ_ASSIGN(
            section.payload,
            reader.blob("extension section payload", remaining_extension_bytes));
        extension_bytes += section.payload.size();
        if (!section_ids.insert(section.type_id).second) {
            return std::unexpected(make_error(
                CheckpointErrorCode::DuplicateIdentifier,
                "checkpoint section type IDs must be unique"));
        }
        if (section.type_id == mechanical_response_section_type_v2) {
            if (section.section_revision != foundation_registry_section_revision_v2) {
                return std::unexpected(make_error(
                    CheckpointErrorCode::UnsupportedSimulationContract,
                    "mechanical-response section revision is unsupported"));
            }
            if (auto decoded = decode_mechanical_response_section(section.payload, checkpoint, limits);
                !decoded) {
                return std::unexpected(decoded.error());
            }
            found_mechanical_responses = true;
        } else if (section.type_id == disc_collider_section_type_v2) {
            if (section.section_revision != foundation_registry_section_revision_v2) {
                return std::unexpected(make_error(
                    CheckpointErrorCode::UnsupportedSimulationContract,
                    "disc-collider section revision is unsupported"));
            }
            if (auto decoded = decode_disc_collider_section(section.payload, checkpoint, limits); !decoded) {
                return std::unexpected(decoded.error());
            }
            found_disc_colliders = true;
        } else if (section.type_id == foundation_execution_configuration_section_type_v2) {
            if (section.section_revision != foundation_execution_configuration_section_revision_v2) {
                return std::unexpected(make_error(
                    CheckpointErrorCode::UnsupportedExecutionConfiguration,
                    "execution-configuration section revision is unsupported"));
            }
            if (auto decoded =
                    decode_execution_configuration_section(section.payload, checkpoint, limits);
                !decoded) {
                return std::unexpected(decoded.error());
            }
            found_execution_configuration = true;
        } else {
            checkpoint.extension_sections.push_back(std::move(section));
        }
    }
    if (!found_mechanical_responses || !found_disc_colliders || !found_execution_configuration) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "checkpoint is missing a required typed foundation section"));
    }
#undef PRINCIPIA_READ_ASSIGN

    if (!reader.finished()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::TrailingData,
            "unexpected data after checkpoint payload",
            reader.offset()));
    }
    canonicalize(checkpoint);
    if (auto validity = validate_checkpoint_v2(checkpoint); !validity) {
        return std::unexpected(validity.error());
    }
    return checkpoint;
}

std::expected<RestoredFoundationV2, CheckpointError> restore_checkpoint_v2(
    const FoundationCheckpointV2& checkpoint,
    const SimulationContractV2& supported_contract,
    const FoundationExecutionConfiguration2& supported_execution_configuration)
{
    auto canonical = checkpoint;
    canonicalize(canonical);
    if (auto validity = validate_checkpoint_v2(canonical); !validity) {
        return std::unexpected(validity.error());
    }
    if (auto supported = require_supported_contract(canonical, supported_contract); !supported) {
        return std::unexpected(supported.error());
    }
    if (auto supported = require_supported_execution_configuration(
            canonical,
            supported_execution_configuration);
        !supported) {
        return std::unexpected(supported.error());
    }
    if (!canonical.extension_sections.empty()) {
        return std::unexpected(make_error(
            CheckpointErrorCode::UnsupportedSimulationContract,
            "checkpoint contains a required registry section unknown to this runtime"));
    }
    auto restored_execution_configuration =
        restore_execution_configuration(canonical.execution_configuration);
    if (!restored_execution_configuration) {
        return std::unexpected(restored_execution_configuration.error());
    }

    std::vector<materials::MaterialDefinition> material_definitions;
    material_definitions.reserve(canonical.materials.size());
    for (const auto& material : canonical.materials) {
        material_definitions.push_back(materials::MaterialDefinition{
            materials::MaterialId{material.id},
            material.name,
            materials::CompositionId{material.composition_id},
            materials::MechanicalModelId{material.mechanical_model_id},
            materials::ThermalModelId{material.thermal_model_id},
            materials::ElectricalModelId{material.electrical_model_id},
            materials::MagneticModelId{material.magnetic_model_id},
            materials::OpticalModelId{material.optical_model_id},
            materials::PhaseModelId{material.phase_model_id},
        });
    }
    materials::MaterialRegistry restored_materials;
    if (!restored_materials.restore(
            canonical.material_revision,
            std::move(material_definitions),
            restore_catalog(canonical.material_catalog))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "material registry rejected the validated checkpoint"));
    }

    std::vector<materials::MechanicalResponseDefinition> mechanical_definitions;
    mechanical_definitions.reserve(canonical.mechanical_responses.size());
    for (const auto& response : canonical.mechanical_responses) {
        materials::ImpactResponseKind impact_kind{};
        switch (response.impact_kind) {
        case ImpactResponseKindV2::FrictionlessRestitution:
            impact_kind = materials::ImpactResponseKind::FrictionlessRestitution;
            break;
        default:
            return std::unexpected(make_error(
                CheckpointErrorCode::InvalidEnum,
                "checkpoint impact-response kind is unknown"));
        }
        mechanical_definitions.push_back(materials::MechanicalResponseDefinition{
            materials::MechanicalModelId{response.id},
            response.name,
            impact_kind,
            response.normal_coefficient_of_restitution,
        });
    }
    materials::MechanicalResponseRegistry restored_mechanical_responses;
    if (!restored_mechanical_responses.restore(
            canonical.mechanical_response_revision,
            std::move(mechanical_definitions))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "mechanical-response registry rejected the validated checkpoint"));
    }

    std::vector<boundaries::BoundaryDefinition> boundary_definitions;
    boundary_definitions.reserve(canonical.boundaries.size());
    for (const auto& boundary : canonical.boundaries) {
        boundaries::BoundaryDefinition definition{
            boundaries::BoundaryId{boundary.id},
            boundary.name,
            boundaries::AxisAlignedBox2{
                spacetime::WorldPosition<2>{
                    units::metres(boundary.minimum_x_metres),
                    units::metres(boundary.minimum_y_metres)},
                spacetime::WorldPosition<2>{
                    units::metres(boundary.maximum_x_metres),
                    units::metres(boundary.maximum_y_metres)},
            },
            {},
        };
        for (const auto& condition : boundary.conditions) {
            auto runtime_condition = restore_condition(condition.condition);
            if (!runtime_condition) {
                return std::unexpected(runtime_condition.error());
            }
            definition.conditions.emplace(state::StateChannelId{condition.channel_id}, *runtime_condition);
        }
        boundary_definitions.push_back(std::move(definition));
    }

    world::WorldState2 restored_world;
    if (!restored_world.boundaries.restore(canonical.boundary_revision, std::move(boundary_definitions))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "boundary registry rejected the validated checkpoint"));
    }
    world::ParticleSnapshot2 particle_snapshot;
    particle_snapshot.revision = canonical.particle_revision;
    particle_snapshot.particles.reserve(canonical.particles.size());
    for (const auto& particle : canonical.particles) {
        particle_snapshot.particles.push_back(world::ParticleState2{
            world::ParticleId{particle.id},
            spacetime::WorldPosition<2>{
                units::metres(particle.position_x_metres),
                units::metres(particle.position_y_metres)},
            math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(
                    particle.momentum_x_kilogram_metres_per_second),
                units::kilogram_metres_per_second(
                    particle.momentum_y_kilogram_metres_per_second)},
            units::kilograms(particle.rest_mass_kilograms),
            materials::MaterialId{particle.material_id},
            particle.constraint == ParticleConstraintV2::Free
                ? world::KinematicConstraint::Free
                : world::KinematicConstraint::Fixed,
        });
    }
    if (!restored_world.particles.restore(std::move(particle_snapshot))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "particle store rejected the validated checkpoint"));
    }
    restored_world.tick = canonical.world_tick;
    restored_world.simulation_time =
        spacetime::SimulationTime{units::seconds(canonical.simulation_time_seconds)};

    std::vector<world::DiscCollider2> collider_definitions;
    collider_definitions.reserve(canonical.disc_colliders.size());
    for (const auto& collider : canonical.disc_colliders) {
        collider_definitions.push_back(world::DiscCollider2{
            world::ParticleId{collider.particle_id},
            units::metres(collider.radius_metres),
        });
    }
    world::DiscColliderRegistry2 restored_disc_colliders;
    if (!restored_disc_colliders.restore(
            canonical.disc_collider_revision,
            std::move(collider_definitions))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "disc-collider registry rejected the validated checkpoint"));
    }

    auto policy = restore_policy(canonical.gravity_composition_policy);
    if (!policy) {
        return std::unexpected(policy.error());
    }
    std::vector<operators::GravityOperatorNode> operator_nodes;
    operator_nodes.reserve(canonical.gravity_operators.size());
    for (const auto& node : canonical.gravity_operators) {
        operator_nodes.push_back(operators::GravityOperatorNode{
            operators::OperatorId{node.id},
            node.priority,
            operators::CircularRegion2{
                spacetime::WorldPosition<2>{
                    units::metres(node.support_center_x_metres),
                    units::metres(node.support_center_y_metres)},
                units::metres(node.support_radius_metres),
            },
            restore_transform(node.transform),
        });
    }
    operators::GravityOperatorGraph restored_operators{*policy};
    if (!restored_operators.restore(
            *policy,
            canonical.gravity_operator_revision,
            std::move(operator_nodes))) {
        return std::unexpected(make_error(
            CheckpointErrorCode::InvalidDefinition,
            "gravity graph rejected the validated checkpoint"));
    }

    return RestoredFoundationV2{
        std::move(restored_world),
        std::move(restored_materials),
        canonical.base_gravity,
        std::move(restored_operators),
        std::move(restored_mechanical_responses),
        std::move(restored_disc_colliders),
        canonical.simulation_contract,
        canonical.semantic_seed_mixer_version,
        canonical.world_seed,
        canonical.particle_ids,
        canonical.boundary_ids,
        canonical.gravity_operator_ids,
        *restored_execution_configuration,
    };
}

}  // namespace principia::serialization
