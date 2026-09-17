#include "../test_support.hpp"

#include <principia/core/deterministic_rng.hpp>
#include <principia/core/monotonic_id_source.hpp>
#include <principia/boundaries/boundary.hpp>
#include <principia/materials/material.hpp>
#include <principia/math/vector.hpp>
#include <principia/ontology/registry.hpp>
#include <principia/operators/gravity_operator.hpp>
#include <principia/scheduler/scheduler.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/state/channel.hpp>
#include <principia/units/quantity.hpp>
#include <principia/world/chunk_coordinates.hpp>
#include <principia/world/chunk_grid.hpp>
#include <principia/world/particle.hpp>
#include <principia/world/simulation_resolution.hpp>

#include <concepts>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct TinyIdTag;
using TinyId = principia::core::StrongId<TinyIdTag, std::uint8_t>;

template <typename Left, typename Right>
concept Addable = requires(Left left, Right right) { left + right; };

template <typename Left, typename Right>
concept Subtractable = requires(Left left, Right right) { left - right; };

static_assert(Addable<principia::units::Length, principia::units::Length>);
static_assert(!Addable<principia::units::Length, principia::units::Duration>);
static_assert(!Addable<principia::units::TemperaturePoint, principia::units::TemperaturePoint>);
static_assert(Subtractable<principia::units::TemperaturePoint, principia::units::TemperaturePoint>);
static_assert(!std::convertible_to<principia::world::ParticleId, principia::spacetime::FrameId>);

[[nodiscard]] principia::scheduler::SolverExecutionContract test_solver_contract()
{
    using namespace principia;
    return scheduler::SolverExecutionContract{
        ontology::theory_ids::newtonian_particle_dynamics,
        scheduler::ValidityContractRevision{1},
        scheduler::StepContractRevision{1},
        scheduler::IntegratorContractRevision{1},
        scheduler::StepControlPolicy::Fixed,
        scheduler::IntegratorFamily::OperatorSplit,
        {},
        {},
        {},
        scheduler::DeterministicNumericPolicy{
            scheduler::DeterminismGuarantee::BitwiseWithinBuild,
            scheduler::FloatingPointPolicy::StrictIeee754,
            scheduler::NonFinitePolicy::RejectProposal,
            scheduler::IterationOrderPolicy::CanonicalPersistentId,
            scheduler::ParallelAccumulationPolicy::SerialCanonical,
        },
    };
}

[[nodiscard]] principia::ontology::TheoryDescriptor minimal_theory(
    principia::ontology::TheoryId id,
    std::string name)
{
    return principia::ontology::TheoryDescriptor{
        .id = id,
        .name = std::move(name),
        .validity_domain = principia::ontology::ValidityDomainId{1},
    };
}

[[nodiscard]] bool ontology_construction_rejected(
    std::vector<principia::ontology::TheoryDescriptor> theories,
    std::vector<principia::ontology::ModelBridgeDescriptor> bridges = {})
{
    try {
        principia::ontology::TheoryRegistry registry{std::move(theories), std::move(bridges)};
        static_cast<void>(registry);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

void test_foundation()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    core::MonotonicIdSource<world::ParticleId> ids;
    const auto first = ids.allocate();
    const auto second = ids.allocate();
    require(first.has_value() && second.has_value(), "monotonic IDs should allocate");
    require(first->value() == 1 && second->value() == 2 && ids.next_value() == 3,
            "persistent IDs must advance monotonically");

    const auto seed_a = core::mix_semantic_seed(42, 7, "world-generation", 9);
    const auto seed_b = core::mix_semantic_seed(42, 7, "world-generation", 9);
    const auto seed_other = core::mix_semantic_seed(42, 7, "particles", 9);
    require(seed_a == seed_b && seed_a != seed_other, "semantic random streams must be stable and isolated");
    require(core::mix_semantic_seed(42, 7, "world-generation", 9) !=
                core::mix_semantic_seed(7, 42, "world-generation", 9),
            "semantic seed components must be domain-separated rather than XOR-commutative");

    core::MonotonicIdSource<TinyId> tiny_ids;
    for (std::uint16_t value = 1; value <= std::numeric_limits<std::uint8_t>::max(); ++value) {
        const auto allocated = tiny_ids.allocate();
        require(allocated.has_value() && allocated->value() == static_cast<std::uint8_t>(value),
                "the maximum representable strong ID must be allocated exactly once");
    }
    require(!tiny_ids.allocate().has_value() && tiny_ids.exhausted() && tiny_ids.next_value() == 0,
            "ID exhaustion must be explicit and must never wrap to a valid identifier");

    world::ParticleStore2 restore_target;
    world::ParticleSnapshot2 impossible_particle_snapshot{
        0,
        {world::ParticleState2{
            world::ParticleId{1},
            spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)},
            math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(0.0),
                units::kilogram_metres_per_second(0.0)},
            units::kilograms(1.0),
            materials::MaterialId{1},
            world::KinematicConstraint::Free}}};
    const auto impossible_restore = restore_target.restore(std::move(impossible_particle_snapshot));
    require(!impossible_restore && impossible_restore.error() == world::ParticleRestoreError::InvalidRevision &&
                restore_target.ordered_particles().empty() && restore_target.revision() == 0,
            "persistent particle restore must reject an impossible source revision atomically");

    const auto decomposed = world::decompose_global_cell(spacetime::CellPosition{-1, -1}, 32);
    require(decomposed.chunk == spacetime::ChunkPosition{-1, -1}, "negative cells must floor-divide into negative chunks");
    require(decomposed.local == spacetime::CellPosition{31, 31}, "negative cell local coordinate must wrap into the chunk");
    const auto extreme = world::decompose_global_cell(
        spacetime::CellPosition{std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()},
        3);
    require(extreme.local == spacetime::CellPosition{1, 1},
            "minimum signed global coordinates must decompose without overflow");

    world::ChunkGrid2<int, 4, 1> left;
    world::ChunkGrid2<int, 4, 1> right;
    for (std::size_t y = 0; y < 4; ++y) {
        left.interior(3, y) = 10 + static_cast<int>(y);
        right.interior(0, y) = 20 + static_cast<int>(y);
    }

    world::ChunkResolutionProfile resolution;
    require(!resolution.set(ontology::TheoryId{}, world::ResolutionLevel::Resolved) &&
                !resolution.set(
                    ontology::theory_ids::newtonian_particle_dynamics,
                    static_cast<world::ResolutionLevel>(255U)) &&
                resolution.set(
                    ontology::theory_ids::newtonian_particle_dynamics,
                    world::ResolutionLevel::Resolved) &&
                resolution.for_theory(ontology::theory_ids::newtonian_particle_dynamics) ==
                    world::ResolutionLevel::Resolved,
            "resolution metadata must reject reserved theory IDs and unknown level discriminants");
    world::exchange_horizontal_halos(left, right);
    for (std::size_t y = 0; y < 4; ++y) {
        require(left.storage(5, y + 1) == 20 + static_cast<int>(y), "right interior must populate left halo");
        require(right.storage(0, y + 1) == 10 + static_cast<int>(y), "left interior must populate right halo");
    }

    const auto channels = state::make_standard_channel_registry();
    require(channels.find(state::standard_channels::position_id) != nullptr, "position channel must be registered");
    require(channels.find(state::standard_channels::effective_gravity_id) != nullptr,
            "effective gravity channel must be registered");
    state::ChannelRegistry malformed_channels;
    require(!malformed_channels.register_channel(state::ChannelKey<double>{state::StateChannelId{}},
                                                   "invalid", state::FieldLocation::NotSpatial) &&
                !malformed_channels.register_channel(state::ChannelKey<double>{state::StateChannelId{700}},
                                                      " \t", state::FieldLocation::NotSpatial) &&
                !malformed_channels.register_channel(
                    state::ChannelKey<double>{state::StateChannelId{701}},
                    "invalid location",
                    static_cast<state::FieldLocation>(255U)),
            "channel registration must reject reserved IDs, blank names, and unknown locations");

    operators::GravityOperatorGraph forward{operators::CompositionPolicy::TransformChain};
    operators::GravityOperatorGraph reverse{operators::CompositionPolicy::TransformChain};
    const operators::CircularRegion2 region{
        spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)}, units::metres(10.0)};
    const operators::GravityOperatorNode low_priority{
        operators::OperatorId{2}, 10, region, operators::RotateGravityQuarterTurns{1}};
    const operators::GravityOperatorNode high_priority{
        operators::OperatorId{1}, 20, region, operators::ScaleGravity{2.0}};
    require(forward.add(low_priority) && forward.add(high_priority), "valid gravity nodes should install");
    require(reverse.add(high_priority) && reverse.add(low_priority), "reverse insertion should install");

    const fields::GravityVector<2> down{
        units::metres_per_second_squared(0.0), units::metres_per_second_squared(-1.0)};
    const spacetime::Event<2> origin{
        spacetime::SimulationTime{},
        spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)},
        spacetime::FrameId{1},
    };
    const operators::CircularRegion2 invalid_region{
        spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)}, units::metres(-1.0)};
    require(!invalid_region.contains(origin.position),
            "standalone support-region queries must fail closed for invalid geometry");
    const auto maximum_finite = std::numeric_limits<double>::max();
    const operators::CircularRegion2 overflow_resistant_region{
        spacetime::WorldPosition<2>{units::metres(-maximum_finite), units::metres(0.0)},
        units::metres(maximum_finite)};
    require(!overflow_resistant_region.contains(spacetime::WorldPosition<2>{
                units::metres(maximum_finite), units::metres(0.0)}),
            "finite extreme coordinates must not turn an overflowing squared distance into a false hit");
    const auto result_a = forward.apply(down, origin);
    const auto result_b = reverse.apply(down, origin);
    require_near(units::in_metres_per_second_squared(result_a[0]), 2.0, 1.0e-12,
                 "declared transform chain should rotate then scale");
    require(result_a == result_b, "composition must not depend on insertion order");
    const auto graph_revision = forward.revision();
    auto invalid_node = low_priority;
    invalid_node.id = operators::OperatorId{99};
    invalid_node.support.center[0] = units::metres(std::numeric_limits<double>::quiet_NaN());
    require(!forward.add(invalid_node) && forward.revision() == graph_revision,
            "invalid gravity operators must not mutate the graph or its revision");
    bool null_base_rejected = false;
    try {
        operators::OperatedGravityField2 invalid_field{
            std::unique_ptr<const fields::EffectiveGravityField<2>>{},
            operators::GravityOperatorGraph{}};
        static_cast<void>(invalid_field);
    } catch (const std::invalid_argument&) {
        null_base_rejected = true;
    }
    require(null_base_rejected, "an operated field must reject a null physical base at construction");
    bool invalid_policy_rejected = false;
    try {
        operators::GravityOperatorGraph invalid_policy{static_cast<operators::CompositionPolicy>(255U)};
        static_cast<void>(invalid_policy);
    } catch (const std::invalid_argument&) {
        invalid_policy_rejected = true;
    }
    require(invalid_policy_rejected, "gravity graphs must reject unknown composition-policy discriminants");
    bool empty_sampler_rejected = false;
    try {
        fields::AnalyticField<fields::GravityVector<2>, 2> invalid_analytic{
            fields::effective_gravity_metadata<2>(),
            fields::AnalyticField<fields::GravityVector<2>, 2>::Sampler{}};
        static_cast<void>(invalid_analytic);
    } catch (const std::invalid_argument&) {
        empty_sampler_rejected = true;
    }
    require(empty_sampler_rejected, "an analytic field must reject an empty sampling function at construction");
    bool invalid_field_metadata_rejected = false;
    try {
        auto metadata = fields::effective_gravity_metadata<2>();
        metadata.storage = static_cast<fields::FieldStorageKind>(255U);
        fields::ConstantField<fields::GravityVector<2>, 2> invalid_field{metadata, down};
        static_cast<void>(invalid_field);
    } catch (const std::invalid_argument&) {
        invalid_field_metadata_rejected = true;
    }
    require(invalid_field_metadata_rejected,
            "field construction must reject unknown persistent storage discriminants");
    auto wrong_gravity_id = fields::effective_gravity_metadata<2>();
    wrong_gravity_id.id = fields::FieldTypeId{99};
    auto wrong_gravity_location = fields::effective_gravity_metadata<2>();
    wrong_gravity_location.location = state::FieldLocation::ParticleCarried;
    auto wrong_gravity_components = fields::effective_gravity_metadata<2>();
    wrong_gravity_components.physical_components = 1;
    require(!fields::valid_effective_gravity_metadata<2>(wrong_gravity_id) &&
                !fields::valid_effective_gravity_metadata<2>(wrong_gravity_location) &&
                !fields::valid_effective_gravity_metadata<2>(wrong_gravity_components),
            "effective gravity identity, canonical location, and component count must validate independently");

    materials::MaterialRegistry material_registry;
    const materials::MaterialDefinition invalid_material{
        materials::MaterialId{1},
        "invalid",
        materials::CompositionId{1},
        materials::MechanicalModelId{},
        materials::ThermalModelId{1},
        materials::ElectricalModelId{1},
        materials::MagneticModelId{1},
        materials::OpticalModelId{1},
        materials::PhaseModelId{1},
    };
    require(!material_registry.try_add(invalid_material) && material_registry.revision() == 0,
            "invalid constitutive references must not enter or revise the material registry");
    auto blank_material = invalid_material;
    blank_material.id = materials::MaterialId{2};
    blank_material.name = " \t";
    blank_material.mechanical = materials::MechanicalModelId{1};
    const auto blank_material_result = material_registry.try_add(blank_material);
    require(!blank_material_result && blank_material_result.error() == materials::MaterialRegistryError::EmptyName,
            "material metadata names must contain a visible character");
    materials::MaterialModelCatalog invalid_catalog;
    invalid_catalog.mechanical_models = std::set{materials::MechanicalModelId{}};
    const auto invalid_catalog_restore = material_registry.restore(0, {}, invalid_catalog);
    require(!invalid_catalog_restore &&
                invalid_catalog_restore.error() == materials::MaterialRegistryError::InvalidMechanicalModelId &&
                material_registry.empty() && material_registry.revision() == 0,
            "material catalog restore must reject reserved foreign keys atomically");
    auto valid_material = invalid_material;
    valid_material.name = "valid";
    valid_material.mechanical = materials::MechanicalModelId{1};
    const auto impossible_material_revision = material_registry.restore(2, {valid_material});
    require(!impossible_material_revision &&
                impossible_material_revision.error() == materials::MaterialRegistryError::InvalidRevision &&
                material_registry.empty() && material_registry.revision() == 0,
            "add-only material restore must reject revisions not equal to its live definition count");

    boundaries::BoundaryRegistry boundary_registry;
    const boundaries::BoundaryDefinition incompatible_boundary{
        boundaries::BoundaryId{1},
        "incompatible",
        boundaries::AxisAlignedBox2{
            spacetime::WorldPosition<2>{units::metres(0.0), units::metres(0.0)},
            spacetime::WorldPosition<2>{units::metres(1.0), units::metres(1.0)}},
        {{state::standard_channels::temperature_id, boundaries::BoundaryConditionKind::MechanicallyRigid}},
    };
    const auto incompatible_result = boundary_registry.try_add(incompatible_boundary);
    require(!incompatible_result &&
                incompatible_result.error() == boundaries::BoundaryRegistryError::IncompatibleChannelCondition &&
                boundary_registry.revision() == 0,
            "boundary conditions must be compatible with the state channel they constrain");
    auto blank_boundary = incompatible_boundary;
    blank_boundary.id = boundaries::BoundaryId{2};
    blank_boundary.name = "\r\n";
    blank_boundary.conditions.clear();
    const auto blank_boundary_result = boundary_registry.try_add(blank_boundary);
    require(!blank_boundary_result && blank_boundary_result.error() == boundaries::BoundaryRegistryError::EmptyName,
            "boundary metadata names must contain a visible character");

    const auto descriptors_a = channels.ordered_descriptors();
    const auto descriptors_b = channels.ordered_descriptors();
    require(descriptors_a.size() == descriptors_b.size() && descriptors_a.data() != descriptors_b.data(),
            "channel inspection must return independent snapshots without shared mutable cache storage");

    const auto& ontology = ontology::standard_theory_registry();
    require(ontology.theories().size() == 50, "the complete metadata ladder plus first effective layer must be registered");
    require(ontology.implemented_theories().size() == 2, "only the first effective physical layer may be implemented");
    const auto* quantum_gravity = ontology.find(ontology::theory_ids::quantum_gravity_unknown);
    require(quantum_gravity != nullptr &&
                quantum_gravity->status == ontology::ImplementationStatus::MetadataOnly,
            "quantum gravity must remain ontology-only");
    const auto ontology_edges = ontology.edges();
    const auto limit_edge = std::ranges::find_if(ontology_edges, [](const ontology::TheoryEdge& edge) {
        return edge.source == ontology::theory_ids::relativistic_particle_mechanics &&
               edge.target == ontology::theory_ids::newtonian_particle_dynamics &&
               edge.kind == ontology::TheoryRelationKind::LimitingTheory;
    });
    require(limit_edge != ontology_edges.end() && limit_edge->limit == ontology::limit_parameters::beta &&
                limit_edge->verification == ontology::verification_suites::relativistic_to_newtonian_particle_limit,
            "the low-velocity ontology edge must carry beta and its future verification contract");
    require(ontology::name_of(ontology::constraints::fixed_kinematic) == "fixed kinematic",
            "every implemented constraint ID must have a stable inspection name");

    auto reserved_metadata = minimal_theory(ontology::TheoryId{700}, "ReservedMetadata");
    reserved_metadata.conserved = {ontology::ConservedQuantityId{}};
    require(ontology_construction_rejected({std::move(reserved_metadata)}),
            "ontology construction must reject reserved metadata IDs independently");

    auto invalid_implementation = minimal_theory(ontology::TheoryId{700}, "InvalidImplementation");
    invalid_implementation.status = static_cast<ontology::ImplementationStatus>(255U);
    require(ontology_construction_rejected({std::move(invalid_implementation)}),
            "ontology construction must reject unknown implementation-status discriminants");

    auto invalid_epistemic = minimal_theory(ontology::TheoryId{700}, "InvalidEpistemic");
    invalid_epistemic.epistemic_status = static_cast<ontology::EpistemicStatus>(255U);
    require(ontology_construction_rejected({std::move(invalid_epistemic)}),
            "ontology construction must reject unknown epistemic-status discriminants");

    auto invalid_relation_kind = minimal_theory(ontology::TheoryId{700}, "InvalidRelationKind");
    invalid_relation_kind.relations.push_back(ontology::TheoryRelation{
        ontology::TheoryId{701},
        static_cast<ontology::TheoryRelationKind>(255U),
        ontology::RelationDirection::ThisToOther,
    });
    require(ontology_construction_rejected(
                {std::move(invalid_relation_kind), minimal_theory(ontology::TheoryId{701}, "RelationTarget")}),
            "ontology construction must reject unknown relation-kind discriminants");

    auto invalid_relation_direction = minimal_theory(ontology::TheoryId{700}, "InvalidRelationDirection");
    invalid_relation_direction.relations.push_back(ontology::TheoryRelation{
        ontology::TheoryId{701},
        ontology::TheoryRelationKind::Coupling,
        static_cast<ontology::RelationDirection>(255U),
    });
    require(ontology_construction_rejected(
                {std::move(invalid_relation_direction),
                 minimal_theory(ontology::TheoryId{701}, "DirectionTarget")}),
            "ontology construction must reject unknown relation-direction discriminants");

    std::vector<ontology::TheoryDescriptor> bridge_theories;
    bridge_theories.push_back(minimal_theory(ontology::TheoryId{700}, "BridgeSource"));
    bridge_theories.push_back(minimal_theory(ontology::TheoryId{701}, "BridgeTarget"));
    std::vector<ontology::ModelBridgeDescriptor> invalid_bridges{
        ontology::ModelBridgeDescriptor{
            .id = ontology::ModelBridgeId{700},
            .name = "InvalidBridgeKind",
            .source = ontology::TheoryId{700},
            .target = ontology::TheoryId{701},
            .kind = static_cast<ontology::ModelBridgeKind>(255U),
        },
    };
    require(ontology_construction_rejected(std::move(bridge_theories), std::move(invalid_bridges)),
            "ontology construction must reject unknown bridge-kind discriminants");

    const state::StateChannelId cycle_x{900};
    const state::StateChannelId cycle_y{901};
    const std::vector<scheduler::SolverDescriptor> coupled_descriptors{
        {scheduler::SolverId{2}, "B", state::AccessDescriptor{state::ChannelSet{cycle_x}, state::ChannelSet{cycle_y}},
         test_solver_contract()},
        {scheduler::SolverId{1}, "A", state::AccessDescriptor{state::ChannelSet{cycle_y}, state::ChannelSet{cycle_x}},
         test_solver_contract()},
    };
    const auto coupled_graph = scheduler::try_build_solver_graph(coupled_descriptors);
    require(coupled_graph.success(), "a numerical read/write cycle must be represented rather than rejected");
    require(coupled_graph.graph->strongly_connected_components().size() == 1 && coupled_graph.graph->contains_cycles(),
            "a coupled numerical cycle must collapse to one SCC");

    const std::vector<scheduler::SolverDescriptor> conflicting_writers{
        {scheduler::SolverId{1}, "writer A", state::AccessDescriptor{{}, state::ChannelSet{cycle_x}},
         test_solver_contract()},
        {scheduler::SolverId{2}, "writer B", state::AccessDescriptor{{}, state::ChannelSet{cycle_x}},
         test_solver_contract()},
    };
    require(!scheduler::try_build_solver_graph(conflicting_writers).success(),
            "ambiguous write/write conflicts must not inherit registration order");

    const scheduler::ConservationReport incomplete_audit{
        {{ontology::conserved_quantities::linear_momentum, 0.0, 1.0e-12}}, true, false};
    require(!incomplete_audit.satisfied(),
            "a zero residual cannot pass when external sources and boundary fluxes were not accounted");
    const scheduler::ConservationReport complete_audit{
        {{ontology::conserved_quantities::linear_momentum, 0.0, 1.0e-12}}, true, true};
    require(complete_audit.satisfied(), "an evaluated, source-complete audit within tolerance should pass");
}
