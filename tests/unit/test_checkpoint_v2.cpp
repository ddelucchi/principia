#include "../test_support.hpp"

#include <principia/serialization/checkpoint.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using principia::tests::require;

[[nodiscard]] principia::materials::MaterialModelCatalog make_catalog()
{
    using namespace principia::materials;
    return MaterialModelCatalog{
        std::set<CompositionId>{CompositionId{11}},
        std::set<MechanicalModelId>{MechanicalModelId{12}},
        std::set<ThermalModelId>{ThermalModelId{13}},
        std::set<ElectricalModelId>{ElectricalModelId{14}},
        std::set<MagneticModelId>{MagneticModelId{15}},
        std::set<OpticalModelId>{OpticalModelId{16}},
        std::set<PhaseModelId>{PhaseModelId{17}},
    };
}

[[nodiscard]] principia::materials::MaterialDefinition make_material()
{
    using namespace principia::materials;
    return MaterialDefinition{
        MaterialId{7},
        "checkpoint alloy",
        CompositionId{11},
        MechanicalModelId{12},
        ThermalModelId{13},
        ElectricalModelId{14},
        MagneticModelId{15},
        OpticalModelId{16},
        PhaseModelId{17},
    };
}

[[nodiscard]] principia::boundaries::BoundaryDefinition make_boundary()
{
    using namespace principia;
    return boundaries::BoundaryDefinition{
        boundaries::BoundaryId{5},
        "rigid thermal wall",
        boundaries::AxisAlignedBox2{
            spacetime::WorldPosition<2>{units::metres(-3.0), units::metres(-2.0)},
            spacetime::WorldPosition<2>{units::metres(4.0), units::metres(6.0)},
        },
        {
            {state::standard_channels::position_id, boundaries::BoundaryConditionKind::MechanicallyRigid},
            {state::standard_channels::temperature_id,
             boundaries::BoundaryConditionKind::ThermallyConductive},
        },
    };
}

struct Fixture {
    principia::world::WorldState2 world;
    principia::materials::MaterialRegistry materials;
    principia::materials::MechanicalResponseRegistry mechanical_responses;
    principia::world::DiscColliderRegistry2 disc_colliders;
    principia::operators::GravityOperatorGraph gravity{
        principia::operators::CompositionPolicy::TransformChain};
    principia::serialization::FoundationCaptureContextV2 context;
};

[[nodiscard]] Fixture make_fixture()
{
    using namespace principia;
    Fixture fixture;

    require(
        fixture.materials.restore(1, std::vector<materials::MaterialDefinition>{make_material()}, make_catalog())
            .has_value(),
        "fixture material restore should succeed");
    require(
        fixture.world.boundaries.restore(5, std::vector<boundaries::BoundaryDefinition>{make_boundary()})
            .has_value(),
        "fixture boundary restore should succeed");

    world::ParticleSnapshot2 particles;
    particles.revision = 9;
    particles.particles.push_back(world::ParticleState2{
        world::ParticleId{42},
        spacetime::WorldPosition<2>{units::metres(1.25), units::metres(-8.5)},
        math::Vector<2, units::Momentum>{
            units::kilogram_metres_per_second(3.5),
            units::kilogram_metres_per_second(-2.25),
        },
        units::kilograms(6.75),
        materials::MaterialId{7},
        world::KinematicConstraint::Free,
    });
    require(fixture.world.particles.restore(std::move(particles)).has_value(),
            "fixture particle restore should succeed");
    require(
        fixture.mechanical_responses
            .restore(
                1,
                std::vector<materials::MechanicalResponseDefinition>{
                    materials::MechanicalResponseDefinition{
                        materials::MechanicalModelId{12},
                        "frictionless alloy response",
                        materials::ImpactResponseKind::FrictionlessRestitution,
                        0.625,
                    }})
            .has_value(),
        "fixture mechanical-response restore should succeed");
    require(
        fixture.disc_colliders
            .restore(
                5,
                std::vector<world::DiscCollider2>{
                    world::DiscCollider2{world::ParticleId{42}, units::metres(0.75)}})
            .has_value(),
        "fixture disc-collider restore should succeed");
    fixture.world.tick = 1234;
    fixture.world.simulation_time = spacetime::SimulationTime{units::seconds(10.25)};

    require(
        fixture.gravity
            .restore(
                operators::CompositionPolicy::TransformChain,
                3,
                std::vector<operators::GravityOperatorNode>{operators::GravityOperatorNode{
                    operators::OperatorId{9},
                    -4,
                    operators::CircularRegion2{
                        spacetime::WorldPosition<2>{units::metres(2.0), units::metres(3.0)},
                        units::metres(12.0),
                    },
                    operators::RotateGravityQuarterTurns{-3},
                }})
            .has_value(),
        "fixture gravity restore should succeed");

    const auto solver_contract = solvers::NewtonianParticleSolver2{}.descriptor().contract;
    fixture.context.simulation_contract = serialization::SimulationContractV2{
        solver_contract.theory.value(),
        std::string{solvers::newtonian_integrator_id},
        solver_contract.integrator_revision.value(),
        solver_contract.validity_revision.value(),
        solver_contract.step_revision.value(),
        serialization::foundation_inertial_frame_id_v2,
        1.0 / 120.0,
        2,
        3,
        4,
        serialization::NumericAbiV2{},
    };
    fixture.context.world_seed = 0x0123456789ABCDEFULL;
    fixture.context.particle_ids = serialization::AllocatorCursorV2{43, false};
    fixture.context.boundary_ids = serialization::AllocatorCursorV2{6, false};
    fixture.context.gravity_operator_ids = serialization::AllocatorCursorV2{10, false};
    fixture.context.base_gravity = serialization::ConstantGravityFieldV2{
        fields::effective_newtonian_gravity_field_id.value(),
        "EffectiveNewtonianGravityField",
        static_cast<std::uint8_t>(state::FieldLocation::CellCentered),
        static_cast<std::uint8_t>(fields::FieldStorageKind::Constant),
        2,
        0.0,
        -9.80665,
    };
    fixture.context.execution_configuration = serialization::FoundationExecutionConfiguration2{
        solvers::NewtonianValidityConfiguration{
            units::metres_per_second(123'456'789.0),
            0.025,
        },
        true,
        solvers::FrictionlessContactSystemConfiguration{
            17,
            3,
            2,
            2.5e-11,
            7.5e-10,
        },
    };
    return fixture;
}

[[nodiscard]] principia::serialization::FoundationCheckpointV2 capture_fixture(const Fixture& fixture)
{
    auto checkpoint = principia::serialization::capture_checkpoint_v2(
        fixture.world,
        fixture.materials,
        fixture.mechanical_responses,
        fixture.disc_colliders,
        fixture.gravity,
        fixture.context);
    require(checkpoint.has_value(), "complete fixture capture should succeed");
    return std::move(*checkpoint);
}

constexpr std::size_t execution_configuration_payload_bytes =
    1U + sizeof(double) + sizeof(double) + sizeof(std::uint32_t) +
    sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(double) + sizeof(double);
constexpr std::size_t extension_section_header_bytes =
    sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);

[[nodiscard]] std::uint64_t read_u64_little_endian(std::string_view bytes, std::size_t offset)
{
    require(offset <= bytes.size() && bytes.size() - offset >= sizeof(std::uint64_t),
            "wire test offset must remain inside the checkpoint");
    std::uint64_t value{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(
                     static_cast<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint32_t read_u32_little_endian(std::string_view bytes, std::size_t offset)
{
    require(offset <= bytes.size() && bytes.size() - offset >= sizeof(std::uint32_t),
            "wire test offset must remain inside the checkpoint");
    std::uint32_t value{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

void write_u64_little_endian(std::string& bytes, std::size_t offset, std::uint64_t value)
{
    require(offset <= bytes.size() && bytes.size() - offset >= sizeof(value),
            "wire mutation must remain inside the checkpoint");
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_u32_little_endian(std::string& bytes, std::size_t offset, std::uint32_t value)
{
    require(offset <= bytes.size() && bytes.size() - offset >= sizeof(value),
            "wire mutation must remain inside the checkpoint");
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::size_t execution_configuration_section_offset(std::string_view encoded)
{
    const auto section_bytes = extension_section_header_bytes + execution_configuration_payload_bytes;
    require(encoded.size() >= section_bytes,
            "encoded fixture must contain the fixed-width execution section");
    const auto section_offset = encoded.size() - section_bytes;
    require(
        read_u32_little_endian(encoded, section_offset) ==
                principia::serialization::foundation_execution_configuration_section_type_v2 &&
            read_u32_little_endian(encoded, section_offset + sizeof(std::uint32_t)) ==
                principia::serialization::foundation_execution_configuration_section_revision_v2 &&
            read_u64_little_endian(
                encoded,
                section_offset + sizeof(std::uint32_t) + sizeof(std::uint32_t)) ==
                execution_configuration_payload_bytes,
        "type-3 execution section must be the final canonical fixture section with fixed width");
    return section_offset;
}

void test_complete_round_trip()
{
    using namespace principia;
    require(solvers::deterministic_numeric_abi == serialization::NumericAbiV2{}.id &&
                solvers::deterministic_numeric_abi_revision == serialization::NumericAbiV2{}.revision,
            "solver and checkpoint persistent execution identities must remain harmonized");
    const auto fixture = make_fixture();
    const auto checkpoint = capture_fixture(fixture);
    require(checkpoint.particle_revision == 9, "particle revision must be captured exactly");
    require(checkpoint.boundary_revision == 5, "boundary revision must be captured exactly");
    require(checkpoint.material_revision == 1, "material revision must be captured exactly");
    require(checkpoint.gravity_operator_revision == 3, "operator revision must be captured exactly");
    require(checkpoint.mechanical_response_revision == 1,
            "mechanical-response revision must be captured exactly");
    require(checkpoint.disc_collider_revision == 5,
            "disc-collider revision must be captured exactly");
    require(checkpoint.execution_configuration ==
                serialization::FoundationExecutionConfigurationRecordV2{
                    true,
                    123'456'789.0,
                    0.025,
                    17,
                    3,
                    2,
                    2.5e-11,
                    7.5e-10,
                },
            "non-default validity and contact execution configuration must be captured exactly");
    require(checkpoint.material_catalog.present && checkpoint.material_catalog.mechanical_models.known,
            "material catalog capability metadata must be captured");

    const auto encoded_a = serialization::encode_checkpoint_v2(checkpoint);
    const auto encoded_b = serialization::encode_checkpoint_v2(checkpoint);
    require(encoded_a.has_value() && encoded_b.has_value(), "valid checkpoint should encode");
    require(*encoded_a == *encoded_b, "checkpoint encoding must be byte-canonical");

    const auto decoded = serialization::decode_checkpoint_v2(*encoded_a);
    require(decoded.has_value(), "encoded checkpoint should decode");
    require(*decoded == checkpoint, "decode must retain the complete canonical DTO");

    auto restored = serialization::restore_checkpoint_v2(
        *decoded,
        fixture.context.simulation_contract,
        fixture.context.execution_configuration);
    require(restored.has_value(), "supported complete checkpoint should restore atomically");
    require(restored->world.tick == fixture.world.tick, "world tick must round-trip");
    require(units::in_seconds(restored->world.simulation_time.elapsed()) == 10.25,
            "simulation clock must round-trip");
    require(restored->world.particles.revision() == 9, "particle source stamp must round-trip");
    require(restored->world.boundaries.revision() == 5, "boundary source stamp must round-trip");
    require(restored->materials.revision() == 1, "material source stamp must round-trip");
    require(restored->gravity_operators.revision() == 3, "operator source stamp must round-trip");
    require(restored->mechanical_responses.revision() == 1 &&
                restored->mechanical_responses.find(materials::MechanicalModelId{12}) != nullptr,
            "mechanical-response definitions and source stamp must round-trip");
    require(restored->disc_colliders.revision() == 5 &&
                restored->disc_colliders.find(world::ParticleId{42}) != nullptr,
            "disc-collider definitions and source stamp must round-trip");
    require(restored->materials.model_catalog() != nullptr &&
                restored->materials.model_catalog()->mechanical_models.has_value(),
            "restored registry must retain catalog-known versus catalog-unknown metadata");
    require(restored->particle_ids == fixture.context.particle_ids &&
                restored->boundary_ids == fixture.context.boundary_ids &&
                restored->gravity_operator_ids == fixture.context.gravity_operator_ids,
            "allocator cursors must round-trip exactly");
    require(restored->world_seed == fixture.context.world_seed,
            "world seed must round-trip exactly");
    require(restored->execution_configuration == fixture.context.execution_configuration,
            "native solver execution configuration must round-trip exactly");

    serialization::FoundationCaptureContextV2 recapture_context{
        restored->simulation_contract,
        restored->world_seed,
        restored->particle_ids,
        restored->boundary_ids,
        restored->gravity_operator_ids,
        restored->base_gravity,
        restored->execution_configuration,
    };
    const auto recaptured = serialization::capture_checkpoint_v2(
        restored->world,
        restored->materials,
        restored->mechanical_responses,
        restored->disc_colliders,
        restored->gravity_operators,
        recapture_context);
    require(recaptured.has_value(), "restored foundation should be immediately recapturable");
    const auto reencoded = serialization::encode_checkpoint_v2(*recaptured);
    require(reencoded.has_value() && *reencoded == *encoded_a,
            "restore/capture must be a byte-identical fixed point");
}

void test_canonical_order_and_validation()
{
    using namespace principia;
    auto checkpoint = capture_fixture(make_fixture());
    checkpoint.gravity_operators[0].transform = serialization::RotateGravityV2{-3};
    const auto normalized = serialization::encode_checkpoint_v2(checkpoint);
    require(normalized.has_value(), "equivalent negative rotation should encode");
    checkpoint.gravity_operators[0].transform = serialization::RotateGravityV2{1};
    const auto canonical = serialization::encode_checkpoint_v2(checkpoint);
    require(canonical.has_value() && *canonical == *normalized,
            "equivalent quarter turns must have one canonical encoding");

    auto unresolved = checkpoint;
    unresolved.particles[0].material_id = 999;
    const auto unresolved_error = serialization::validate_checkpoint_v2(unresolved);
    require(!unresolved_error.has_value() &&
                unresolved_error.error().code == serialization::CheckpointErrorCode::UnresolvedMaterial,
            "particle material foreign keys must be checked");

    auto stale_allocator = checkpoint;
    stale_allocator.particle_ids = serialization::AllocatorCursorV2{42, false};
    const auto allocator_error = serialization::validate_checkpoint_v2(stale_allocator);
    require(!allocator_error.has_value() &&
                allocator_error.error().code == serialization::CheckpointErrorCode::InvalidAllocatorState,
            "allocator cursors must not collide with live persistent IDs");

    auto forged_revision = checkpoint;
    forged_revision.material_revision = 2;
    require(!serialization::validate_checkpoint_v2(forged_revision).has_value(),
            "add-only registries must reject unreachable historical revisions");
    forged_revision = checkpoint;
    forged_revision.disc_collider_revision = 2;
    require(!serialization::validate_checkpoint_v2(forged_revision).has_value(),
            "add/remove registries must reject revision/count parity violations");

    auto unresolved_mechanics = checkpoint;
    unresolved_mechanics.mechanical_responses.clear();
    unresolved_mechanics.mechanical_response_revision = 0;
    const auto mechanics_error = serialization::validate_checkpoint_v2(unresolved_mechanics);
    require(!mechanics_error.has_value() &&
                mechanics_error.error().code == serialization::CheckpointErrorCode::InvalidDefinition,
            "every material mechanical model must resolve to a persisted response definition");

    auto orphan_collider = checkpoint;
    orphan_collider.disc_colliders[0].particle_id = 999;
    const auto collider_error = serialization::validate_checkpoint_v2(orphan_collider);
    require(!collider_error.has_value() &&
                collider_error.error().code == serialization::CheckpointErrorCode::InvalidIdentifier,
            "every collider must resolve to a persisted particle");

    auto invalid_restitution = checkpoint;
    invalid_restitution.mechanical_responses[0].normal_coefficient_of_restitution = 1.01;
    require(!serialization::validate_checkpoint_v2(invalid_restitution).has_value(),
            "mechanical restitution must remain inside the constitutive unit interval");

    auto nonfinite = checkpoint;
    nonfinite.base_gravity.y_metres_per_second_squared =
        std::numeric_limits<double>::infinity();
    require(!serialization::encode_checkpoint_v2(nonfinite).has_value(),
            "non-finite field state must never be serialized");
}

void test_execution_configuration_validation_and_sections()
{
    using namespace principia;
    const auto fixture = make_fixture();
    const auto checkpoint = capture_fixture(fixture);

    const auto require_invalid_configuration = [](serialization::FoundationCheckpointV2 candidate,
                                                  serialization::CheckpointErrorCode expected,
                                                  std::string_view message) {
        const auto result = serialization::validate_checkpoint_v2(candidate);
        require(!result.has_value() && result.error().code == expected, message);
    };

    auto invalid = checkpoint;
    invalid.execution_configuration.invariant_speed_metres_per_second = 0.0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "zero invariant speed must be rejected with the execution-configuration cause");
    invalid = checkpoint;
    invalid.execution_configuration.maximum_beta = 0.0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "zero maximum beta must be rejected with the execution-configuration cause");
    invalid = checkpoint;
    invalid.execution_configuration.contact_geometric_tolerance_metres = -1.0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "negative geometric tolerance must be rejected with the execution-configuration cause");
    invalid = checkpoint;
    invalid.execution_configuration.contact_simultaneous_time_tolerance_seconds = -1.0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "negative simultaneous-time tolerance must be rejected with the execution-configuration cause");
    invalid = checkpoint;
    invalid.execution_configuration.maximum_contact_bodies = 0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "zero contact-body capacity must be rejected with the execution-configuration cause");
    invalid = checkpoint;
    invalid.execution_configuration.maximum_contact_boundaries = 0;
    require_invalid_configuration(
        invalid,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "zero contact-boundary capacity must be rejected with the execution-configuration cause");

    constexpr double serialization::FoundationExecutionConfigurationRecordV2::* scalar_members[]{
        &serialization::FoundationExecutionConfigurationRecordV2::invariant_speed_metres_per_second,
        &serialization::FoundationExecutionConfigurationRecordV2::maximum_beta,
        &serialization::FoundationExecutionConfigurationRecordV2::contact_geometric_tolerance_metres,
        &serialization::FoundationExecutionConfigurationRecordV2::contact_simultaneous_time_tolerance_seconds,
    };
    for (const auto member : scalar_members) {
        auto nonfinite_scalar = checkpoint;
        nonfinite_scalar.execution_configuration.*member =
            std::numeric_limits<double>::quiet_NaN();
        require_invalid_configuration(
            nonfinite_scalar,
            serialization::CheckpointErrorCode::NonFiniteValue,
            "every non-finite execution scalar must report the non-finite cause");
    }

    auto zero_events = checkpoint;
    zero_events.execution_configuration.maximum_contact_events = 0;
    const auto zero_events_encoded = serialization::encode_checkpoint_v2(zero_events);
    require(zero_events_encoded.has_value(),
            "zero maximum contact events is an intentional valid fail-before-first-event budget");
    const auto zero_events_decoded = serialization::decode_checkpoint_v2(*zero_events_encoded);
    require(zero_events_decoded.has_value() &&
                zero_events_decoded->execution_configuration.maximum_contact_events == 0,
            "the full u32 contact-event budget domain must round-trip exactly");

    auto disabled_with_collider = checkpoint;
    disabled_with_collider.execution_configuration.contact_enabled = false;
    require_invalid_configuration(
        disabled_with_collider,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "contact-disabled checkpoints must not retain disc colliders that require the contact overload");

    auto disabled_without_collider = disabled_with_collider;
    disabled_without_collider.disc_colliders.clear();
    disabled_without_collider.disc_collider_revision = 6;
    require(serialization::validate_checkpoint_v2(disabled_without_collider).has_value(),
            "contact-disabled checkpoints with no colliders must remain valid");

    auto over_body_budget = checkpoint;
    auto second_particle = over_body_budget.particles.front();
    second_particle.id = 43;
    second_particle.position_x_metres += 10.0;
    second_particle.momentum_x_kilogram_metres_per_second = 0.0;
    second_particle.momentum_y_kilogram_metres_per_second = 0.0;
    over_body_budget.particles.push_back(second_particle);
    over_body_budget.particle_ids.next_identifier = 44;
    over_body_budget.disc_colliders.push_back(serialization::DiscColliderRecordV2{43, 0.5});
    over_body_budget.disc_collider_revision = 6;
    over_body_budget.execution_configuration.maximum_contact_bodies = 1;
    require_invalid_configuration(
        over_body_budget,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "enabled contact must reject collider resources above the persisted body budget");

    auto over_boundary_budget = checkpoint;
    auto second_boundary = over_boundary_budget.boundaries.front();
    second_boundary.id = 6;
    second_boundary.name = "second rigid thermal wall";
    over_boundary_budget.boundaries.push_back(std::move(second_boundary));
    over_boundary_budget.boundary_ids.next_identifier = 7;
    over_boundary_budget.execution_configuration.maximum_contact_boundaries = 1;
    require_invalid_configuration(
        over_boundary_budget,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "enabled contact must reject boundary resources above the persisted boundary budget");

    auto fixed_collider = checkpoint;
    fixed_collider.particles.front().constraint = serialization::ParticleConstraintV2::Fixed;
    fixed_collider.particles.front().momentum_x_kilogram_metres_per_second = 0.0;
    fixed_collider.particles.front().momentum_y_kilogram_metres_per_second = 0.0;
    require_invalid_configuration(
        fixed_collider,
        serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
        "enabled contact must reject collider particles that are not free");

    auto reserved = checkpoint;
    reserved.extension_sections.push_back(serialization::CheckpointExtensionSectionV2{
        serialization::foundation_execution_configuration_section_type_v2,
        1,
        "reserved",
    });
    const auto reserved_error = serialization::encode_checkpoint_v2(reserved);
    require(!reserved_error.has_value() &&
                reserved_error.error().code == serialization::CheckpointErrorCode::DuplicateIdentifier,
            "opaque sections must not collide with reserved typed section 3");

    const auto encoded = serialization::encode_checkpoint_v2(checkpoint);
    require(encoded.has_value(), "execution-section wire fixture must encode");
    const auto section_offset = execution_configuration_section_offset(*encoded);
    const auto payload_offset = section_offset + extension_section_header_bytes;

    auto missing = *encoded;
    write_u32_little_endian(missing, section_offset, 1001);
    const auto missing_result = serialization::decode_checkpoint_v2(missing);
    require(!missing_result.has_value() &&
                missing_result.error().code == serialization::CheckpointErrorCode::InvalidDefinition,
            "older V2 payloads without required type 3 must fail closed");

    auto duplicate = *encoded;
    write_u32_little_endian(
        duplicate,
        section_offset,
        serialization::disc_collider_section_type_v2);
    const auto duplicate_result = serialization::decode_checkpoint_v2(duplicate);
    require(!duplicate_result.has_value() &&
                duplicate_result.error().code == serialization::CheckpointErrorCode::DuplicateIdentifier,
            "duplicate typed execution-section IDs must be rejected");

    auto future_revision = *encoded;
    write_u32_little_endian(future_revision, section_offset + sizeof(std::uint32_t), 2);
    const auto future_revision_result = serialization::decode_checkpoint_v2(future_revision);
    require(!future_revision_result.has_value() &&
                future_revision_result.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedExecutionConfiguration,
            "unknown type-3 revisions must be rejected explicitly");

    auto invalid_bool = *encoded;
    invalid_bool[payload_offset] = static_cast<char>(2);
    const auto invalid_bool_result = serialization::decode_checkpoint_v2(invalid_bool);
    require(!invalid_bool_result.has_value() &&
                invalid_bool_result.error().code == serialization::CheckpointErrorCode::InvalidEnum,
            "contact-enabled must use the canonical zero-or-one wire encoding");

    constexpr std::size_t scalar_offsets[]{1U, 9U, 37U, 45U};
    for (const auto scalar_offset : scalar_offsets) {
        auto nonfinite_scalar = *encoded;
        write_u64_little_endian(
            nonfinite_scalar,
            payload_offset + scalar_offset,
            std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()));
        const auto result = serialization::decode_checkpoint_v2(nonfinite_scalar);
        require(!result.has_value() &&
                    result.error().code == serialization::CheckpointErrorCode::NonFiniteValue,
                "every execution scalar must reject non-finite wire values");
    }

    auto mismatched_validity = fixture.context.execution_configuration;
    mismatched_validity.validity.maximum_beta += 0.001;
    const auto validity_mismatch = serialization::restore_checkpoint_v2(
        checkpoint,
        fixture.context.simulation_contract,
        mismatched_validity);
    require(!validity_mismatch.has_value() &&
                validity_mismatch.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedExecutionConfiguration,
            "restore must reject a caller-selected validity configuration mismatch");

    auto mismatched_mode = fixture.context.execution_configuration;
    mismatched_mode.contact_enabled = false;
    const auto mode_mismatch = serialization::restore_checkpoint_v2(
        checkpoint,
        fixture.context.simulation_contract,
        mismatched_mode);
    require(!mode_mismatch.has_value() &&
                mode_mismatch.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedExecutionConfiguration,
            "restore must reject a caller-selected contact-mode mismatch");

    auto mismatched_contact = fixture.context.execution_configuration;
    ++mismatched_contact.contact.maximum_events;
    const auto contact_mismatch = serialization::restore_checkpoint_v2(
        checkpoint,
        fixture.context.simulation_contract,
        mismatched_contact);
    require(!contact_mismatch.has_value() &&
                contact_mismatch.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedExecutionConfiguration,
            "restore must reject a caller-selected contact configuration mismatch");

    auto invalid_supported = fixture.context.execution_configuration;
    invalid_supported.validity.maximum_beta = 0.0;
    const auto invalid_supported_result = serialization::restore_checkpoint_v2(
        checkpoint,
        fixture.context.simulation_contract,
        invalid_supported);
    require(!invalid_supported_result.has_value() &&
                invalid_supported_result.error().code ==
                    serialization::CheckpointErrorCode::InvalidExecutionConfiguration,
            "restore must diagnose an invalid caller-selected execution configuration");
}

void test_contract_and_extension_rejection()
{
    using namespace principia;
    auto checkpoint = capture_fixture(make_fixture());
    const auto supported_contract = checkpoint.simulation_contract;
    const auto supported_execution = make_fixture().context.execution_configuration;
    checkpoint.semantic_seed_mixer_version += 1;
    const auto future_mixer =
        serialization::restore_checkpoint_v2(checkpoint, supported_contract, supported_execution);
    require(!future_mixer.has_value() &&
                future_mixer.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedSeedMixerVersion,
            "restore must reject unknown semantic RNG mixers explicitly");

    checkpoint = capture_fixture(make_fixture());
    const auto current_contract = checkpoint.simulation_contract;
    const auto current_execution = make_fixture().context.execution_configuration;
    checkpoint.simulation_contract.integrator_id = "FutureIntegrator";
    const auto future_integrator =
        serialization::restore_checkpoint_v2(checkpoint, current_contract, current_execution);
    require(!future_integrator.has_value() &&
                future_integrator.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedSimulationContract,
            "restore must reject unknown integrator contracts explicitly");

    checkpoint = capture_fixture(make_fixture());
    checkpoint.extension_sections.push_back(
        serialization::CheckpointExtensionSectionV2{1001, 1, std::string{"future-registry\0bytes", 21}});
    const auto encoded = serialization::encode_checkpoint_v2(checkpoint);
    require(encoded.has_value(), "opaque length-delimited future sections must remain encodable");
    const auto decoded = serialization::decode_checkpoint_v2(*encoded);
    require(decoded.has_value() && decoded->extension_sections == checkpoint.extension_sections,
            "opaque future sections must decode losslessly");
    const auto unsupported_section = serialization::restore_checkpoint_v2(
        *decoded,
        decoded->simulation_contract,
        make_fixture().context.execution_configuration);
    require(!unsupported_section.has_value() &&
                unsupported_section.error().code ==
                    serialization::CheckpointErrorCode::UnsupportedSimulationContract,
            "unknown required registry sections must never be silently ignored at restore");
}

void test_decode_limits_and_framing()
{
    using namespace principia;
    const auto encoded = serialization::encode_checkpoint_v2(capture_fixture(make_fixture()));
    require(encoded.has_value(), "fixture checkpoint should encode");

    serialization::CheckpointDecodeLimitsV2 limits;
    limits.maximum_input_bytes = encoded->size() - 1;
    const auto input_limit = serialization::decode_checkpoint_v2(*encoded, limits);
    require(!input_limit.has_value() &&
                input_limit.error().code == serialization::CheckpointErrorCode::InputLimitExceeded,
            "decode must enforce the caller byte budget before parsing");

    limits = {};
    limits.maximum_particles = 0;
    const auto particle_limit = serialization::decode_checkpoint_v2(*encoded, limits);
    require(!particle_limit.has_value() &&
                particle_limit.error().code == serialization::CheckpointErrorCode::ParticleLimitExceeded,
            "decode must enforce particle allocation limits before reserve");

    const auto require_limit = [&encoded](
                                   serialization::CheckpointDecodeLimitsV2 constrained,
                                   serialization::CheckpointErrorCode expected,
                                   std::string_view message) {
        const auto result = serialization::decode_checkpoint_v2(*encoded, constrained);
        require(!result.has_value() && result.error().code == expected, message);
    };
    limits = {};
    limits.maximum_boundaries = 0;
    require_limit(limits, serialization::CheckpointErrorCode::BoundaryLimitExceeded,
                  "decode must enforce boundary limits before reserve");
    limits = {};
    limits.maximum_boundary_conditions = 1;
    require_limit(limits, serialization::CheckpointErrorCode::BoundaryConditionLimitExceeded,
                  "decode must enforce cumulative boundary-condition limits");
    limits = {};
    limits.maximum_materials = 0;
    require_limit(limits, serialization::CheckpointErrorCode::MaterialLimitExceeded,
                  "decode must enforce material limits before reserve");
    limits = {};
    limits.maximum_catalog_entries = 6;
    require_limit(limits, serialization::CheckpointErrorCode::CatalogEntryLimitExceeded,
                  "decode must enforce cumulative catalog limits");
    limits = {};
    limits.maximum_gravity_operators = 0;
    require_limit(limits, serialization::CheckpointErrorCode::GravityOperatorLimitExceeded,
                  "decode must enforce gravity-operator limits before reserve");
    limits = {};
    limits.maximum_mechanical_responses = 0;
    require_limit(limits, serialization::CheckpointErrorCode::MechanicalResponseLimitExceeded,
                  "decode must enforce mechanical-response limits before reserve");
    limits = {};
    limits.maximum_disc_colliders = 0;
    require_limit(limits, serialization::CheckpointErrorCode::ColliderLimitExceeded,
                  "decode must enforce disc-collider limits before reserve");
    limits = {};
    limits.maximum_string_bytes = 1;
    require_limit(limits, serialization::CheckpointErrorCode::StringLimitExceeded,
                  "decode must enforce cumulative string-byte limits before allocation");

    auto with_extension = capture_fixture(make_fixture());
    with_extension.extension_sections.push_back(
        serialization::CheckpointExtensionSectionV2{1001, 1, "required"});
    const auto extension_encoded = serialization::encode_checkpoint_v2(with_extension);
    require(extension_encoded.has_value(), "extension limit fixture should encode");
    limits = {};
    limits.maximum_extension_sections = 0;
    const auto extension_limit = serialization::decode_checkpoint_v2(*extension_encoded, limits);
    require(!extension_limit.has_value() &&
                extension_limit.error().code == serialization::CheckpointErrorCode::ExtensionLimitExceeded,
            "decode must enforce extension-section limits before reserve");

    auto trailing = *encoded;
    trailing.push_back('\0');
    const auto trailing_error = serialization::decode_checkpoint_v2(trailing);
    require(!trailing_error.has_value() &&
                trailing_error.error().code == serialization::CheckpointErrorCode::TrailingData,
            "decode must reject trailing payload bytes");

    for (std::size_t length = 0; length < encoded->size(); ++length) {
        require(!serialization::decode_checkpoint_v2(
                     std::string_view{encoded->data(), length})
                     .has_value(),
                "every strict prefix must be rejected as a truncated checkpoint");
    }
}

void test_transactional_registry_restore()
{
    using namespace principia;
    boundaries::BoundaryRegistry boundaries;
    require(boundaries.try_add(make_boundary()).has_value(), "baseline boundary should install");
    const auto boundary_revision = boundaries.revision();
    const auto failed_boundary = boundaries.restore(
        8,
        std::vector<boundaries::BoundaryDefinition>{make_boundary(), make_boundary()});
    require(!failed_boundary.has_value() && boundaries.revision() == boundary_revision && boundaries.size() == 1,
            "failed boundary import must leave the live registry untouched");

    materials::MaterialRegistry materials;
    require(materials.try_add(make_material()).has_value(), "baseline material should install");
    const auto material_revision = materials.revision();
    auto invalid_material = make_material();
    invalid_material.name.clear();
    const auto failed_material = materials.restore(
        8,
        std::vector<materials::MaterialDefinition>{std::move(invalid_material)},
        make_catalog());
    require(!failed_material.has_value() && materials.revision() == material_revision && materials.size() == 1,
            "failed material import must leave the live registry untouched");

    operators::GravityOperatorGraph graph{operators::CompositionPolicy::TransformChain};
    const auto good_node = operators::GravityOperatorNode{
        operators::OperatorId{1},
        0,
        operators::CircularRegion2{
            spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)},
            units::metres(1.0),
        },
        operators::ScaleGravity{1.0},
    };
    require(graph.add(good_node), "baseline gravity node should install");
    const auto graph_revision = graph.revision();
    auto invalid_node = good_node;
    invalid_node.id = operators::OperatorId{2};
    invalid_node.support.radius = units::metres(-1.0);
    const auto failed_graph = graph.restore(
        operators::CompositionPolicy::TransformChain,
        1,
        std::vector<operators::GravityOperatorNode>{invalid_node});
    require(!failed_graph.has_value() && graph.revision() == graph_revision && graph.ordered_nodes().size() == 1,
            "failed gravity import must leave the live graph untouched");

    materials::MechanicalResponseRegistry mechanical_responses;
    const materials::MechanicalResponseDefinition response{
        materials::MechanicalModelId{12},
        "baseline response",
        materials::ImpactResponseKind::FrictionlessRestitution,
        0.5,
    };
    require(mechanical_responses.try_add(response).has_value(),
            "baseline mechanical response should install");
    const auto response_revision = mechanical_responses.revision();
    auto invalid_response = response;
    invalid_response.normal_coefficient_of_restitution = -0.1;
    const auto failed_response = mechanical_responses.restore(
        2,
        std::vector<materials::MechanicalResponseDefinition>{invalid_response});
    require(!failed_response.has_value() && mechanical_responses.revision() == response_revision &&
                mechanical_responses.size() == 1,
            "failed mechanical-response import must leave the live registry untouched");

    world::DiscColliderRegistry2 colliders;
    const world::DiscCollider2 collider{world::ParticleId{42}, units::metres(0.5)};
    require(colliders.try_add(collider).has_value(), "baseline collider should install");
    const auto collider_revision = colliders.revision();
    const auto failed_collider = colliders.restore(
        2,
        std::vector<world::DiscCollider2>{
            world::DiscCollider2{world::ParticleId{42}, units::metres(0.0)}});
    require(!failed_collider.has_value() && colliders.revision() == collider_revision && colliders.size() == 1,
            "failed collider import must leave the live registry untouched");
}

}  // namespace

int main()
{
    try {
        test_complete_round_trip();
        test_canonical_order_and_validation();
        test_execution_configuration_validation_and_sections();
        test_contract_and_extension_rejection();
        test_decode_limits_and_framing();
        test_transactional_registry_restore();
        std::cout << "[PASS] checkpoint_v2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] checkpoint_v2: " << error.what() << '\n';
        return 1;
    }
}
