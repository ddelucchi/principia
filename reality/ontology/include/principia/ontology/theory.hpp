#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/state/channel.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace principia::ontology {

struct TheoryIdTag;
struct ConservedQuantityIdTag;
struct ConstraintIdTag;
struct ValidityDomainIdTag;
struct ModelBridgeIdTag;
struct BoundaryRequirementIdTag;
struct LimitParameterIdTag;
struct VerificationSuiteIdTag;

using TheoryId = core::StrongId<TheoryIdTag, std::uint32_t>;
using ConservedQuantityId = core::StrongId<ConservedQuantityIdTag, std::uint32_t>;
using ConstraintId = core::StrongId<ConstraintIdTag, std::uint32_t>;
using ValidityDomainId = core::StrongId<ValidityDomainIdTag, std::uint32_t>;
using ModelBridgeId = core::StrongId<ModelBridgeIdTag, std::uint32_t>;
using BoundaryRequirementId = core::StrongId<BoundaryRequirementIdTag, std::uint32_t>;
using LimitParameterId = core::StrongId<LimitParameterIdTag, std::uint32_t>;
using VerificationSuiteId = core::StrongId<VerificationSuiteIdTag, std::uint32_t>;

// These values are persistence identifiers. Existing assignments must never be
// renumbered; new theories receive new values.
namespace theory_ids {

inline constexpr TheoryId causal_structure{1};
inline constexpr TheoryId lorentzian_spacetime{2};
inline constexpr TheoryId variational_dynamics{3};
inline constexpr TheoryId classical_state{4};
inline constexpr TheoryId quantum_state{5};
inline constexpr TheoryId special_relativity{6};
inline constexpr TheoryId relativistic_particle_mechanics{7};
inline constexpr TheoryId newtonian_mechanics{8};
inline constexpr TheoryId classical_field_theory{9};
inline constexpr TheoryId maxwell_electrodynamics{10};
inline constexpr TheoryId electrostatics{11};
inline constexpr TheoryId magnetostatics{12};
inline constexpr TheoryId classical_optics{13};
inline constexpr TheoryId general_relativity{14};
inline constexpr TheoryId newtonian_gravity{15};
inline constexpr TheoryId einstein_maxwell{16};
inline constexpr TheoryId quantum_mechanics{17};
inline constexpr TheoryId atomic_physics{18};
inline constexpr TheoryId molecular_physics{19};
inline constexpr TheoryId many_body_quantum_mechanics{20};
inline constexpr TheoryId quantum_field_theory{21};
inline constexpr TheoryId qed{22};
inline constexpr TheoryId qcd{23};
inline constexpr TheoryId electroweak_theory{24};
inline constexpr TheoryId standard_model{25};
inline constexpr TheoryId nuclear_effective_theory{26};
inline constexpr TheoryId nuclear_physics{27};
inline constexpr TheoryId statistical_mechanics{28};
inline constexpr TheoryId thermodynamics{29};
inline constexpr TheoryId kinetic_theory{30};
inline constexpr TheoryId continuum_mechanics{31};
inline constexpr TheoryId elasticity{32};
inline constexpr TheoryId plasticity{33};
inline constexpr TheoryId fracture{34};
inline constexpr TheoryId hydrodynamics{35};
inline constexpr TheoryId euler_flow{36};
inline constexpr TheoryId navier_stokes{37};
inline constexpr TheoryId heat_transport{38};
inline constexpr TheoryId plasma_physics{39};
inline constexpr TheoryId magnetohydrodynamics{40};
inline constexpr TheoryId relativistic_hydrodynamics{41};
inline constexpr TheoryId relativistic_mhd{42};
inline constexpr TheoryId stellar_physics{43};
inline constexpr TheoryId compact_object_physics{44};
inline constexpr TheoryId cosmology{45};
inline constexpr TheoryId qft_curved_spacetime{46};
inline constexpr TheoryId semiclassical_gravity{47};
inline constexpr TheoryId quantum_gravity_unknown{48};
inline constexpr TheoryId newtonian_particle_dynamics{49};
inline constexpr TheoryId effective_newtonian_gravity_field{50};

}  // namespace theory_ids

namespace conserved_quantities {

inline constexpr ConservedQuantityId mass{1};
inline constexpr ConservedQuantityId linear_momentum{2};
inline constexpr ConservedQuantityId angular_momentum{3};
inline constexpr ConservedQuantityId energy{4};
inline constexpr ConservedQuantityId electric_charge{5};
inline constexpr ConservedQuantityId probability{6};
inline constexpr ConservedQuantityId baryon_number{7};
inline constexpr ConservedQuantityId magnetic_flux{8};
inline constexpr ConservedQuantityId particle_number{9};

}  // namespace conserved_quantities

namespace constraints {

inline constexpr ConstraintId mass_shell{1};
inline constexpr ConstraintId quantum_state_normalization{2};
inline constexpr ConstraintId gauss_electric{3};
inline constexpr ConstraintId gauss_magnetic{4};
inline constexpr ConstraintId gauge_condition{5};
inline constexpr ConstraintId einstein_hamiltonian{6};
inline constexpr ConstraintId einstein_momentum{7};
inline constexpr ConstraintId equation_of_state{8};
inline constexpr ConstraintId positive_density{9};
inline constexpr ConstraintId local_equilibrium{10};
inline constexpr ConstraintId strain_compatibility{11};
inline constexpr ConstraintId yield_surface{12};
inline constexpr ConstraintId crack_irreversibility{13};
inline constexpr ConstraintId solenoidal_magnetic_field{14};
inline constexpr ConstraintId charge_neutrality{15};
inline constexpr ConstraintId fixed_kinematic{16};

}  // namespace constraints

namespace validity_domains {

inline constexpr ValidityDomainId structural{1};
inline constexpr ValidityDomainId stationary_action{2};
inline constexpr ValidityDomainId decohered_classical{3};
inline constexpr ValidityDomainId quantum_state{4};
inline constexpr ValidityDomainId flat_spacetime{5};
inline constexpr ValidityDomainId relativistic_particle{6};
inline constexpr ValidityDomainId low_velocity{7};
inline constexpr ValidityDomainId classical_field{8};
inline constexpr ValidityDomainId relativistic_electromagnetism{9};
inline constexpr ValidityDomainId electrostatic{10};
inline constexpr ValidityDomainId magnetostatic{11};
inline constexpr ValidityDomainId geometric_optics{12};
inline constexpr ValidityDomainId classical_curved_spacetime{13};
inline constexpr ValidityDomainId weak_field_slow_motion{14};
inline constexpr ValidityDomainId nonrelativistic_quantum{15};
inline constexpr ValidityDomainId bound_atomic{16};
inline constexpr ValidityDomainId born_oppenheimer{17};
inline constexpr ValidityDomainId quantum_many_body{18};
inline constexpr ValidityDomainId relativistic_quantum_field{19};
inline constexpr ValidityDomainId low_energy_nuclear{20};
inline constexpr ValidityDomainId thermodynamic_limit{21};
inline constexpr ValidityDomainId local_equilibrium{22};
inline constexpr ValidityDomainId dilute_gas{23};
inline constexpr ValidityDomainId continuum{24};
inline constexpr ValidityDomainId small_strain{25};
inline constexpr ValidityDomainId constitutive_plasticity{26};
inline constexpr ValidityDomainId fracture_scale{27};
inline constexpr ValidityDomainId hydrodynamic{28};
inline constexpr ValidityDomainId inviscid{29};
inline constexpr ValidityDomainId newtonian_fluid{30};
inline constexpr ValidityDomainId near_equilibrium_transport{31};
inline constexpr ValidityDomainId plasma_collective{32};
inline constexpr ValidityDomainId magnetohydrodynamic{33};
inline constexpr ValidityDomainId relativistic_fluid{34};
inline constexpr ValidityDomainId relativistic_magnetofluid{35};
inline constexpr ValidityDomainId stellar_structure{36};
inline constexpr ValidityDomainId compact_object{37};
inline constexpr ValidityDomainId cosmological_scale{38};
inline constexpr ValidityDomainId curved_spacetime_below_cutoff{39};
inline constexpr ValidityDomainId semiclassical_backreaction{40};
inline constexpr ValidityDomainId unknown_quantum_gravity{41};

}  // namespace validity_domains

namespace model_bridges {

inline constexpr ModelBridgeId relativistic_to_newtonian_particles{1};
inline constexpr ModelBridgeId relativistic_to_effective_gravity{2};
inline constexpr ModelBridgeId statistical_to_thermodynamic{3};
inline constexpr ModelBridgeId kinetic_to_hydrodynamic{4};
inline constexpr ModelBridgeId plasma_to_magnetohydrodynamic{5};

}  // namespace model_bridges

namespace boundary_requirements {

inline constexpr BoundaryRequirementId particle_domain{1};
inline constexpr BoundaryRequirementId classical_field_domain{2};
inline constexpr BoundaryRequirementId electromagnetic_interface{3};
inline constexpr BoundaryRequirementId gravitational_domain{4};
inline constexpr BoundaryRequirementId quantum_state_domain{5};
inline constexpr BoundaryRequirementId continuum_flux{6};
inline constexpr BoundaryRequirementId mechanical_interface{7};
inline constexpr BoundaryRequirementId thermal_interface{8};
inline constexpr BoundaryRequirementId plasma_interface{9};
inline constexpr BoundaryRequirementId cosmological_domain{10};

}  // namespace boundary_requirements

namespace limit_parameters {

inline constexpr LimitParameterId classical_decoherence{1};
inline constexpr LimitParameterId beta{2};
inline constexpr LimitParameterId quasi_static{3};
inline constexpr LimitParameterId wavelength_to_structure_scale{4};
inline constexpr LimitParameterId gravitational_compactness{5};
inline constexpr LimitParameterId action_to_hbar{6};
inline constexpr LimitParameterId inverse_particle_number{7};
inline constexpr LimitParameterId knudsen_number{8};
inline constexpr LimitParameterId viscosity{9};
inline constexpr LimitParameterId plasma_collisionality{10};

}  // namespace limit_parameters

namespace verification_suites {

inline constexpr VerificationSuiteId none{};
inline constexpr VerificationSuiteId relativistic_to_newtonian_particle_limit{1};
inline constexpr VerificationSuiteId general_relativity_to_newtonian_gravity_limit{2};
inline constexpr VerificationSuiteId maxwell_quasi_static_limits{3};
inline constexpr VerificationSuiteId statistical_to_thermodynamic_limit{4};
inline constexpr VerificationSuiteId kinetic_to_hydrodynamic_limit{5};
inline constexpr VerificationSuiteId plasma_to_magnetohydrodynamic_limit{6};

}  // namespace verification_suites

enum class TheoryRelationKind : std::uint8_t {
    StructuralDependency,
    LimitingTheory,
    EffectiveTheory,
    CoarseGraining,
    Coupling,
    SymmetryBreaking,
    SemiclassicalApproximation,
    UnknownCompletion,
};

// Direction is expressed relative to the descriptor that owns the relation.
enum class RelationDirection : std::uint8_t {
    ThisToOther,
    OtherToThis,
    Bidirectional,
};

enum class ImplementationStatus : std::uint8_t {
    MetadataOnly,
    Implemented,
    Experimental,
    Deprecated,
};

enum class EpistemicStatus : std::uint8_t {
    Established,
    EffectiveEstablished,
    Approximation,
    Speculative,
    FictionalExtension,
};

enum class ModelBridgeKind : std::uint8_t {
    Projection,
    Lifting,
    SharedCanonicalState,
    BidirectionalApproximation,
};

using ChannelSignature = state::AccessDescriptor;
using ConservedQuantitySet = std::vector<ConservedQuantityId>;
using ConstraintSet = std::vector<ConstraintId>;
using BoundaryRequirementSet = std::vector<BoundaryRequirementId>;

struct TheoryRelation {
    TheoryId other;
    TheoryRelationKind kind{TheoryRelationKind::StructuralDependency};
    RelationDirection direction{RelationDirection::OtherToThis};
    std::optional<LimitParameterId> limit;
    VerificationSuiteId verification{verification_suites::none};

    friend bool operator==(const TheoryRelation&, const TheoryRelation&) = default;
};

struct TheoryDescriptor {
    TheoryId id;
    std::string name;
    std::string description;
    std::vector<TheoryRelation> relations;
    ChannelSignature channels;
    ConservedQuantitySet conserved;
    ConstraintSet constraints;
    ValidityDomainId validity_domain;
    std::vector<ModelBridgeId> bridges;
    BoundaryRequirementSet boundaries;
    ImplementationStatus status{ImplementationStatus::MetadataOnly};
    EpistemicStatus epistemic_status{EpistemicStatus::Established};
};

struct ModelBridgeDescriptor {
    ModelBridgeId id;
    std::string name;
    std::string description;
    TheoryId source;
    TheoryId target;
    ModelBridgeKind kind{ModelBridgeKind::Projection};
    ConservedQuantitySet preserves;
};

}  // namespace principia::ontology
