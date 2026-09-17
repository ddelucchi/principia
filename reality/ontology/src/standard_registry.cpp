#include <principia/ontology/registry.hpp>

#include <initializer_list>
#include <utility>

namespace principia::ontology {
namespace {

struct TheoryOptions {
    std::vector<TheoryRelation> relations;
    ChannelSignature channels;
    ConservedQuantitySet conserved;
    ConstraintSet constraints;
    ValidityDomainId validity{validity_domains::structural};
    std::vector<ModelBridgeId> bridges;
    BoundaryRequirementSet boundaries;
    ImplementationStatus status{ImplementationStatus::MetadataOnly};
    EpistemicStatus epistemic{EpistemicStatus::Established};
};

[[nodiscard]] TheoryRelation incoming(
    TheoryId other,
    TheoryRelationKind kind,
    std::optional<LimitParameterId> limit = std::nullopt,
    VerificationSuiteId verification = verification_suites::none)
{
    return TheoryRelation{
        .other = other,
        .kind = kind,
        .direction = RelationDirection::OtherToThis,
        .limit = limit,
        .verification = verification,
    };
}

[[nodiscard]] ChannelSignature channels(
    std::initializer_list<state::StateChannelId> reads,
    std::initializer_list<state::StateChannelId> writes)
{
    return ChannelSignature{state::ChannelSet{reads}, state::ChannelSet{writes}};
}

}  // namespace

TheoryRegistry make_standard_theory_registry()
{
    namespace ids = theory_ids;
    namespace cq = conserved_quantities;
    namespace ct = constraints;
    namespace vd = validity_domains;
    namespace mb = model_bridges;
    namespace br = boundary_requirements;
    namespace lp = limit_parameters;
    namespace vs = verification_suites;
    namespace sc = state::standard_channels;

    std::vector<TheoryDescriptor> theories;
    theories.reserve(50);

    const auto add = [&theories](
                         TheoryId id,
                         std::string name,
                         std::string description,
                         TheoryOptions options = {}) {
        theories.push_back(TheoryDescriptor{
            .id = id,
            .name = std::move(name),
            .description = std::move(description),
            .relations = std::move(options.relations),
            .channels = std::move(options.channels),
            .conserved = std::move(options.conserved),
            .constraints = std::move(options.constraints),
            .validity_domain = options.validity,
            .bridges = std::move(options.bridges),
            .boundaries = std::move(options.boundaries),
            .status = options.status,
            .epistemic_status = options.epistemic,
        });
    };

    add(
        ids::causal_structure,
        "CausalStructure",
        "Ordering and influence structure among physical events.");

    add(
        ids::lorentzian_spacetime,
        "LorentzianSpacetime",
        "Spacetime geometry with a Lorentzian metric and causal cones.",
        {.relations = {incoming(ids::causal_structure, TheoryRelationKind::StructuralDependency)},
         .channels = channels({sc::metric_field_id}, {}),
         .validity = vd::classical_curved_spacetime,
         .boundaries = {br::gravitational_domain}});

    add(
        ids::variational_dynamics,
        "VariationalDynamics",
        "Dynamics derived from stationary-action principles.",
        {.relations = {incoming(ids::causal_structure, TheoryRelationKind::StructuralDependency)},
         .validity = vd::stationary_action});

    add(
        ids::classical_state,
        "ClassicalState",
        "Effective state description with definite classical observables.",
        {.relations = {incoming(
             ids::quantum_state,
             TheoryRelationKind::EffectiveTheory,
             lp::classical_decoherence)},
         .validity = vd::decohered_classical,
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::quantum_state,
        "QuantumState",
        "State structure supporting amplitudes, superposition, and quantum observables.",
        {.relations = {incoming(ids::causal_structure, TheoryRelationKind::StructuralDependency)},
         .conserved = {cq::probability},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::quantum_state,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::special_relativity,
        "SpecialRelativity",
        "Relativistic physics on flat Lorentzian spacetime.",
        {.relations = {incoming(ids::lorentzian_spacetime, TheoryRelationKind::StructuralDependency)},
         .channels = channels({sc::metric_field_id}, {}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .validity = vd::flat_spacetime});

    add(
        ids::relativistic_particle_mechanics,
        "RelativisticParticleMechanics",
        "Classical point-particle mechanics with relativistic momentum-energy kinematics.",
        {.relations = {
             incoming(ids::special_relativity, TheoryRelationKind::StructuralDependency),
             incoming(ids::variational_dynamics, TheoryRelationKind::StructuralDependency),
             incoming(ids::classical_state, TheoryRelationKind::StructuralDependency),
         },
         .channels = channels(
             {sc::position_id, sc::momentum_id, sc::rest_mass_id},
             {sc::position_id, sc::momentum_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::mass_shell},
         .validity = vd::relativistic_particle,
         .bridges = {mb::relativistic_to_newtonian_particles},
         .boundaries = {br::particle_domain}});

    add(
        ids::newtonian_mechanics,
        "NewtonianMechanics",
        "Low-velocity classical mechanics in a preferred inertial frame.",
        {.relations = {incoming(
             ids::relativistic_particle_mechanics,
             TheoryRelationKind::LimitingTheory,
             lp::beta,
             vs::relativistic_to_newtonian_particle_limit)},
         .channels = channels(
             {sc::position_id, sc::momentum_id, sc::rest_mass_id},
             {sc::position_id, sc::momentum_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::fixed_kinematic},
         .validity = vd::low_velocity,
         .boundaries = {br::particle_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::classical_field_theory,
        "ClassicalFieldTheory",
        "Variational dynamics for classical fields over spacetime.",
        {.relations = {
             incoming(ids::variational_dynamics, TheoryRelationKind::StructuralDependency),
             incoming(ids::classical_state, TheoryRelationKind::StructuralDependency),
             incoming(ids::lorentzian_spacetime, TheoryRelationKind::StructuralDependency),
         },
         .validity = vd::classical_field,
         .boundaries = {br::classical_field_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::maxwell_electrodynamics,
        "MaxwellElectrodynamics",
        "Unified relativistic dynamics of electric and magnetic fields.",
        {.relations = {
             incoming(ids::classical_field_theory, TheoryRelationKind::StructuralDependency),
             incoming(ids::special_relativity, TheoryRelationKind::StructuralDependency),
             incoming(
                 ids::qed,
                 TheoryRelationKind::EffectiveTheory,
                 lp::action_to_hbar),
         },
         .channels = channels(
             {sc::charge_density_id, sc::current_density_id, sc::electric_field_id, sc::magnetic_field_id},
             {sc::electric_field_id, sc::magnetic_field_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy, cq::electric_charge},
         .constraints = {ct::gauss_electric, ct::gauss_magnetic, ct::gauge_condition},
         .validity = vd::relativistic_electromagnetism,
         .boundaries = {br::electromagnetic_interface}});

    add(
        ids::electrostatics,
        "Electrostatics",
        "Quasi-static electric-field limit of Maxwell electrodynamics.",
        {.relations = {incoming(
             ids::maxwell_electrodynamics,
             TheoryRelationKind::LimitingTheory,
             lp::quasi_static,
             vs::maxwell_quasi_static_limits)},
         .channels = channels(
             {sc::charge_density_id, sc::electric_field_id},
             {sc::electric_field_id}),
         .conserved = {cq::electric_charge, cq::energy},
         .constraints = {ct::gauss_electric},
         .validity = vd::electrostatic,
         .boundaries = {br::electromagnetic_interface},
         .epistemic = EpistemicStatus::Approximation});

    add(
        ids::magnetostatics,
        "Magnetostatics",
        "Quasi-static magnetic-field limit of Maxwell electrodynamics.",
        {.relations = {incoming(
             ids::maxwell_electrodynamics,
             TheoryRelationKind::LimitingTheory,
             lp::quasi_static,
             vs::maxwell_quasi_static_limits)},
         .channels = channels(
             {sc::current_density_id, sc::magnetic_field_id},
             {sc::magnetic_field_id}),
         .conserved = {cq::magnetic_flux, cq::energy},
         .constraints = {ct::gauss_magnetic},
         .validity = vd::magnetostatic,
         .boundaries = {br::electromagnetic_interface},
         .epistemic = EpistemicStatus::Approximation});

    add(
        ids::classical_optics,
        "ClassicalOptics",
        "Wave and ray optics emerging from Maxwell fields at optical scales.",
        {.relations = {incoming(
             ids::maxwell_electrodynamics,
             TheoryRelationKind::EffectiveTheory,
             lp::wavelength_to_structure_scale)},
         .channels = channels({sc::electric_field_id, sc::magnetic_field_id}, {}),
         .conserved = {cq::energy, cq::linear_momentum, cq::angular_momentum},
         .validity = vd::geometric_optics,
         .boundaries = {br::electromagnetic_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::general_relativity,
        "GeneralRelativity",
        "Classical dynamical spacetime sourced by stress-energy.",
        {.relations = {
             incoming(ids::lorentzian_spacetime, TheoryRelationKind::StructuralDependency),
             incoming(ids::variational_dynamics, TheoryRelationKind::StructuralDependency),
             incoming(ids::classical_field_theory, TheoryRelationKind::StructuralDependency),
         },
         .channels = channels({sc::metric_field_id, sc::stress_tensor_id}, {sc::metric_field_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::einstein_hamiltonian, ct::einstein_momentum},
         .validity = vd::classical_curved_spacetime,
         .bridges = {mb::relativistic_to_effective_gravity},
         .boundaries = {br::gravitational_domain}});

    add(
        ids::newtonian_gravity,
        "NewtonianGravity",
        "Weak-field, slow-motion gravitational potential theory.",
        {.relations = {incoming(
             ids::general_relativity,
             TheoryRelationKind::LimitingTheory,
             lp::gravitational_compactness,
             vs::general_relativity_to_newtonian_gravity_limit)},
         .channels = channels({sc::mass_density_id}, {sc::effective_gravity_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .validity = vd::weak_field_slow_motion,
         .boundaries = {br::gravitational_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::einstein_maxwell,
        "EinsteinMaxwell",
        "Coupled Einstein and Maxwell field equations.",
        {.relations = {
             incoming(ids::general_relativity, TheoryRelationKind::Coupling),
             incoming(ids::maxwell_electrodynamics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::metric_field_id,
              sc::stress_tensor_id,
              sc::charge_density_id,
              sc::current_density_id,
              sc::electric_field_id,
              sc::magnetic_field_id},
             {sc::metric_field_id, sc::electric_field_id, sc::magnetic_field_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy, cq::electric_charge},
         .constraints = {
             ct::einstein_hamiltonian,
             ct::einstein_momentum,
             ct::gauss_electric,
             ct::gauss_magnetic,
         },
         .validity = vd::classical_curved_spacetime,
         .boundaries = {br::gravitational_domain, br::electromagnetic_interface}});

    add(
        ids::quantum_mechanics,
        "QuantumMechanics",
        "Nonrelativistic unitary dynamics of quantum states.",
        {.relations = {
             incoming(ids::quantum_state, TheoryRelationKind::StructuralDependency),
             incoming(ids::variational_dynamics, TheoryRelationKind::StructuralDependency),
         },
         .conserved = {cq::probability, cq::energy},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::nonrelativistic_quantum,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::atomic_physics,
        "AtomicPhysics",
        "Effective quantum description of nuclei and bound electrons.",
        {.relations = {
             incoming(ids::quantum_mechanics, TheoryRelationKind::EffectiveTheory),
             incoming(ids::electrostatics, TheoryRelationKind::Coupling),
         },
         .conserved = {cq::probability, cq::energy, cq::electric_charge},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::bound_atomic,
         .boundaries = {br::quantum_state_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::molecular_physics,
        "MolecularPhysics",
        "Molecular quantum dynamics in a Born-Oppenheimer-style regime.",
        {.relations = {
             incoming(ids::atomic_physics, TheoryRelationKind::StructuralDependency),
             incoming(ids::quantum_mechanics, TheoryRelationKind::EffectiveTheory),
         },
         .conserved = {cq::probability, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::born_oppenheimer,
         .boundaries = {br::quantum_state_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::many_body_quantum_mechanics,
        "ManyBodyQuantumMechanics",
        "Interacting nonrelativistic quantum systems with many degrees of freedom.",
        {.relations = {incoming(ids::quantum_mechanics, TheoryRelationKind::StructuralDependency)},
         .conserved = {
             cq::probability,
             cq::particle_number,
             cq::linear_momentum,
             cq::angular_momentum,
             cq::energy,
         },
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::quantum_many_body,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::quantum_field_theory,
        "QuantumFieldTheory",
        "Relativistic quantum theory with field degrees of freedom.",
        {.relations = {
             incoming(ids::quantum_state, TheoryRelationKind::StructuralDependency),
             incoming(ids::special_relativity, TheoryRelationKind::StructuralDependency),
             incoming(ids::variational_dynamics, TheoryRelationKind::StructuralDependency),
         },
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization, ct::gauge_condition},
         .validity = vd::relativistic_quantum_field,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::qed,
        "QED",
        "Quantum electrodynamics of charged matter and the electromagnetic field.",
        {.relations = {
             incoming(ids::quantum_field_theory, TheoryRelationKind::StructuralDependency),
             incoming(ids::electroweak_theory, TheoryRelationKind::SymmetryBreaking),
         },
         .conserved = {cq::electric_charge, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization, ct::gauge_condition},
         .validity = vd::relativistic_quantum_field,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::qcd,
        "QCD",
        "Quantum chromodynamics of quarks and gluons.",
        {.relations = {incoming(ids::quantum_field_theory, TheoryRelationKind::StructuralDependency)},
         .conserved = {cq::baryon_number, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization, ct::gauge_condition},
         .validity = vd::relativistic_quantum_field,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::electroweak_theory,
        "ElectroweakTheory",
        "Unified electroweak quantum gauge theory before and after symmetry breaking.",
        {.relations = {incoming(ids::quantum_field_theory, TheoryRelationKind::StructuralDependency)},
         .conserved = {cq::electric_charge, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization, ct::gauge_condition},
         .validity = vd::relativistic_quantum_field,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::standard_model,
        "StandardModel",
        "Coupled strong and electroweak quantum field theory of known elementary particles.",
        {.relations = {
             incoming(ids::qcd, TheoryRelationKind::Coupling),
             incoming(ids::electroweak_theory, TheoryRelationKind::Coupling),
         },
         .conserved = {cq::electric_charge, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization, ct::gauge_condition},
         .validity = vd::relativistic_quantum_field,
         .boundaries = {br::quantum_state_domain}});

    add(
        ids::nuclear_effective_theory,
        "NuclearEffectiveTheory",
        "Low-energy effective degrees of freedom inherited from QCD.",
        {.relations = {incoming(ids::qcd, TheoryRelationKind::EffectiveTheory)},
         .conserved = {cq::baryon_number, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .validity = vd::low_energy_nuclear,
         .boundaries = {br::quantum_state_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::nuclear_physics,
        "NuclearPhysics",
        "Structure and reactions of nuclei at sub-QCD resolution.",
        {.relations = {
             incoming(ids::nuclear_effective_theory, TheoryRelationKind::StructuralDependency),
             incoming(ids::quantum_mechanics, TheoryRelationKind::StructuralDependency),
         },
         .conserved = {cq::baryon_number, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::low_energy_nuclear,
         .boundaries = {br::quantum_state_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::statistical_mechanics,
        "StatisticalMechanics",
        "Statistical description of macroscopic behavior from many-body states.",
        {.relations = {
             incoming(ids::many_body_quantum_mechanics, TheoryRelationKind::CoarseGraining),
             incoming(ids::classical_state, TheoryRelationKind::CoarseGraining),
         },
         .channels = channels(
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id},
             {sc::temperature_id, sc::internal_energy_id}),
         .conserved = {cq::mass, cq::particle_number, cq::linear_momentum, cq::energy},
         .validity = vd::thermodynamic_limit,
         .bridges = {mb::statistical_to_thermodynamic},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::thermodynamics,
        "Thermodynamics",
        "Macroscopic equilibrium and near-equilibrium state relations.",
        {.relations = {incoming(
             ids::statistical_mechanics,
             TheoryRelationKind::CoarseGraining,
             lp::inverse_particle_number,
             vs::statistical_to_thermodynamic_limit)},
         .channels = channels(
             {sc::temperature_id, sc::internal_energy_id, sc::mass_density_id},
             {sc::temperature_id, sc::internal_energy_id}),
         .conserved = {cq::mass, cq::energy},
         .constraints = {ct::equation_of_state},
         .validity = vd::local_equilibrium,
         .bridges = {mb::statistical_to_thermodynamic},
         .boundaries = {br::thermal_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::kinetic_theory,
        "KineticTheory",
        "Mesoscopic distribution-function dynamics between particles and continua.",
        {.relations = {incoming(ids::statistical_mechanics, TheoryRelationKind::EffectiveTheory)},
         .channels = channels(
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::particle_number, cq::linear_momentum, cq::energy},
         .constraints = {ct::positive_density},
         .validity = vd::dilute_gas,
         .bridges = {mb::kinetic_to_hydrodynamic},
         .boundaries = {br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::continuum_mechanics,
        "ContinuumMechanics",
        "Continuum balance laws for mass, momentum, and energy.",
        {.relations = {incoming(
             ids::kinetic_theory,
             TheoryRelationKind::CoarseGraining,
             lp::knudsen_number,
             vs::kinetic_to_hydrodynamic_limit)},
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::stress_tensor_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::positive_density},
         .validity = vd::continuum,
         .boundaries = {br::continuum_flux, br::mechanical_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::elasticity,
        "Elasticity",
        "Recoverable constitutive stress response to deformation.",
        {.relations = {incoming(ids::continuum_mechanics, TheoryRelationKind::EffectiveTheory)},
         .channels = channels(
             {sc::strain_tensor_id, sc::temperature_id},
             {sc::stress_tensor_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::strain_compatibility},
         .validity = vd::small_strain,
         .boundaries = {br::mechanical_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::plasticity,
        "Plasticity",
        "Irreversible constitutive deformation beyond a yield surface.",
        {.relations = {
             incoming(ids::continuum_mechanics, TheoryRelationKind::EffectiveTheory),
             incoming(ids::elasticity, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::stress_tensor_id, sc::strain_tensor_id, sc::temperature_id},
             {sc::stress_tensor_id, sc::strain_tensor_id, sc::internal_energy_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::yield_surface},
         .validity = vd::constitutive_plasticity,
         .boundaries = {br::mechanical_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::fracture,
        "Fracture",
        "Creation and propagation of cracks under mechanical loading.",
        {.relations = {
             incoming(ids::elasticity, TheoryRelationKind::EffectiveTheory),
             incoming(ids::plasticity, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::stress_tensor_id, sc::strain_tensor_id, sc::internal_energy_id},
             {sc::stress_tensor_id, sc::strain_tensor_id}),
         .conserved = {cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::crack_irreversibility},
         .validity = vd::fracture_scale,
         .boundaries = {br::mechanical_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::hydrodynamics,
        "Hydrodynamics",
        "Conservative continuum dynamics of fluids.",
        {.relations = {
             incoming(ids::continuum_mechanics, TheoryRelationKind::StructuralDependency),
             incoming(
                 ids::relativistic_hydrodynamics,
                 TheoryRelationKind::LimitingTheory,
                 lp::beta),
             incoming(
                 ids::kinetic_theory,
                 TheoryRelationKind::CoarseGraining,
                 lp::knudsen_number,
                 vs::kinetic_to_hydrodynamic_limit),
         },
         .channels = channels(
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::equation_of_state, ct::positive_density},
         .validity = vd::hydrodynamic,
         .bridges = {mb::kinetic_to_hydrodynamic},
         .boundaries = {br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::euler_flow,
        "EulerFlow",
        "Inviscid compressible-fluid limit of hydrodynamics.",
        {.relations = {incoming(
             ids::hydrodynamics,
             TheoryRelationKind::LimitingTheory,
             lp::viscosity)},
         .channels = channels(
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::equation_of_state, ct::positive_density},
         .validity = vd::inviscid,
         .boundaries = {br::continuum_flux},
         .epistemic = EpistemicStatus::Approximation});

    add(
        ids::navier_stokes,
        "NavierStokes",
        "Viscous Newtonian-fluid realization of hydrodynamics.",
        {.relations = {incoming(ids::hydrodynamics, TheoryRelationKind::EffectiveTheory)},
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::stress_tensor_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::equation_of_state, ct::positive_density},
         .validity = vd::newtonian_fluid,
         .boundaries = {br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::heat_transport,
        "HeatTransport",
        "Continuum transport of internal energy and temperature.",
        {.relations = {
             incoming(ids::thermodynamics, TheoryRelationKind::StructuralDependency),
             incoming(ids::continuum_mechanics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::temperature_id, sc::internal_energy_id, sc::mass_density_id},
             {sc::temperature_id, sc::internal_energy_id}),
         .conserved = {cq::energy},
         .constraints = {ct::equation_of_state},
         .validity = vd::near_equilibrium_transport,
         .boundaries = {br::thermal_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::plasma_physics,
        "PlasmaPhysics",
        "Collective kinetic and electromagnetic dynamics of ionized matter.",
        {.relations = {
             incoming(ids::kinetic_theory, TheoryRelationKind::Coupling),
             incoming(ids::maxwell_electrodynamics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::charge_density_id,
              sc::current_density_id,
              sc::electric_field_id,
              sc::magnetic_field_id},
             {sc::momentum_density_id,
              sc::energy_density_id,
              sc::current_density_id,
              sc::electric_field_id,
              sc::magnetic_field_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy, cq::electric_charge},
         .constraints = {ct::positive_density, ct::gauss_electric, ct::gauss_magnetic},
         .validity = vd::plasma_collective,
         .bridges = {mb::plasma_to_magnetohydrodynamic},
         .boundaries = {br::plasma_interface},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::magnetohydrodynamics,
        "Magnetohydrodynamics",
        "Conducting-fluid coarse graining of plasma and electromagnetic dynamics.",
        {.relations = {
             incoming(
                 ids::plasma_physics,
                 TheoryRelationKind::CoarseGraining,
                 lp::plasma_collisionality,
                 vs::plasma_to_magnetohydrodynamic_limit),
             incoming(ids::hydrodynamics, TheoryRelationKind::Coupling),
             incoming(ids::maxwell_electrodynamics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::magnetic_field_id},
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::magnetic_field_id}),
         .conserved = {
             cq::mass,
             cq::linear_momentum,
             cq::angular_momentum,
             cq::energy,
             cq::magnetic_flux,
         },
         .constraints = {ct::positive_density, ct::solenoidal_magnetic_field},
         .validity = vd::magnetohydrodynamic,
         .bridges = {mb::plasma_to_magnetohydrodynamic},
         .boundaries = {br::plasma_interface, br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::relativistic_hydrodynamics,
        "RelativisticHydrodynamics",
        "Covariant conservative dynamics of relativistic fluids.",
        {.relations = {
             incoming(ids::special_relativity, TheoryRelationKind::StructuralDependency),
             incoming(ids::kinetic_theory, TheoryRelationKind::CoarseGraining, lp::knudsen_number),
         },
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::metric_field_id},
             {sc::mass_density_id, sc::momentum_density_id, sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::equation_of_state, ct::positive_density},
         .validity = vd::relativistic_fluid,
         .boundaries = {br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::relativistic_mhd,
        "RelativisticMHD",
        "Relativistic coupling of conducting fluids and electromagnetic fields.",
        {.relations = {
             incoming(ids::relativistic_hydrodynamics, TheoryRelationKind::Coupling),
             incoming(ids::maxwell_electrodynamics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::electric_field_id,
              sc::magnetic_field_id,
              sc::metric_field_id},
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::electric_field_id,
              sc::magnetic_field_id}),
         .conserved = {
             cq::mass,
             cq::linear_momentum,
             cq::angular_momentum,
             cq::energy,
             cq::electric_charge,
             cq::magnetic_flux,
         },
         .constraints = {
             ct::equation_of_state,
             ct::positive_density,
             ct::gauss_electric,
             ct::gauss_magnetic,
         },
         .validity = vd::relativistic_magnetofluid,
         .boundaries = {br::plasma_interface, br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::stellar_physics,
        "StellarPhysics",
        "Coupled fluid, gravity, radiation, and nuclear models for ordinary stars.",
        {.relations = {
             incoming(ids::hydrodynamics, TheoryRelationKind::Coupling),
             incoming(ids::newtonian_gravity, TheoryRelationKind::Coupling),
             incoming(ids::nuclear_physics, TheoryRelationKind::Coupling),
             incoming(ids::heat_transport, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::temperature_id,
              sc::effective_gravity_id},
             {sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id,
              sc::temperature_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::equation_of_state, ct::positive_density},
         .validity = vd::stellar_structure,
         .boundaries = {br::continuum_flux, br::gravitational_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::compact_object_physics,
        "CompactObjectPhysics",
        "Relativistic gravity and dense-matter models for compact objects.",
        {.relations = {
             incoming(ids::general_relativity, TheoryRelationKind::Coupling),
             incoming(ids::relativistic_hydrodynamics, TheoryRelationKind::Coupling),
             incoming(ids::nuclear_physics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::metric_field_id,
              sc::stress_tensor_id,
              sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id},
             {sc::metric_field_id,
              sc::mass_density_id,
              sc::momentum_density_id,
              sc::energy_density_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy, cq::baryon_number},
         .constraints = {
             ct::einstein_hamiltonian,
             ct::einstein_momentum,
             ct::equation_of_state,
             ct::positive_density,
         },
         .validity = vd::compact_object,
         .boundaries = {br::gravitational_domain, br::continuum_flux},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::cosmology,
        "Cosmology",
        "Large-scale dynamical spacetime with effective cosmic matter content.",
        {.relations = {
             incoming(ids::general_relativity, TheoryRelationKind::StructuralDependency),
             incoming(ids::thermodynamics, TheoryRelationKind::Coupling),
             incoming(ids::statistical_mechanics, TheoryRelationKind::Coupling),
         },
         .channels = channels(
             {sc::metric_field_id, sc::stress_tensor_id, sc::mass_density_id, sc::energy_density_id},
             {sc::metric_field_id, sc::mass_density_id, sc::energy_density_id}),
         .conserved = {cq::energy},
         .constraints = {ct::einstein_hamiltonian, ct::einstein_momentum, ct::positive_density},
         .validity = vd::cosmological_scale,
         .boundaries = {br::cosmological_domain},
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::qft_curved_spacetime,
        "QFTCurvedSpacetime",
        "Quantum fields evolving on a prescribed classical curved spacetime.",
        {.relations = {
             incoming(ids::quantum_field_theory, TheoryRelationKind::SemiclassicalApproximation),
             incoming(ids::general_relativity, TheoryRelationKind::Coupling),
         },
         .channels = channels({sc::metric_field_id}, {}),
         .conserved = {cq::probability},
         .constraints = {ct::quantum_state_normalization},
         .validity = vd::curved_spacetime_below_cutoff,
         .boundaries = {br::quantum_state_domain, br::gravitational_domain},
         .epistemic = EpistemicStatus::Approximation});

    add(
        ids::semiclassical_gravity,
        "SemiclassicalGravity",
        "Classical geometry sourced by expectation values of quantum stress-energy.",
        {.relations = {
             incoming(ids::general_relativity, TheoryRelationKind::SemiclassicalApproximation),
             incoming(ids::qft_curved_spacetime, TheoryRelationKind::Coupling),
         },
         .channels = channels({sc::metric_field_id, sc::stress_tensor_id}, {sc::metric_field_id}),
         .conserved = {cq::energy},
         .constraints = {ct::einstein_hamiltonian, ct::einstein_momentum},
         .validity = vd::semiclassical_backreaction,
         .boundaries = {br::quantum_state_domain, br::gravitational_domain},
         .epistemic = EpistemicStatus::Approximation});

    add(
        ids::quantum_gravity_unknown,
        "QuantumGravityUnknown",
        "Reserved ontology node for an unknown or explicitly fictional quantum-gravity completion.",
        {.relations = {
             incoming(ids::general_relativity, TheoryRelationKind::UnknownCompletion),
             incoming(ids::quantum_field_theory, TheoryRelationKind::UnknownCompletion),
             incoming(ids::semiclassical_gravity, TheoryRelationKind::UnknownCompletion),
         },
         .validity = vd::unknown_quantum_gravity,
         .epistemic = EpistemicStatus::FictionalExtension});

    add(
        ids::newtonian_particle_dynamics,
        "NewtonianParticleDynamics",
        "Implemented low-energy evolution of canonical position, momentum, and rest mass.",
        {.relations = {
             incoming(
                 ids::relativistic_particle_mechanics,
                 TheoryRelationKind::LimitingTheory,
                 lp::beta,
                 vs::relativistic_to_newtonian_particle_limit),
             incoming(ids::newtonian_mechanics, TheoryRelationKind::StructuralDependency),
         },
         .channels = channels(
             {sc::position_id, sc::momentum_id, sc::rest_mass_id, sc::effective_gravity_id},
             {sc::position_id, sc::momentum_id}),
         .conserved = {cq::mass, cq::linear_momentum, cq::angular_momentum, cq::energy},
         .constraints = {ct::fixed_kinematic},
         .validity = vd::low_velocity,
         .bridges = {mb::relativistic_to_newtonian_particles},
         .boundaries = {br::particle_domain},
         .status = ImplementationStatus::Implemented,
         .epistemic = EpistemicStatus::EffectiveEstablished});

    add(
        ids::effective_newtonian_gravity_field,
        "EffectiveNewtonianGravityField",
        "Implemented weak-field acceleration field shared by particle and continuum response.",
        {.relations = {
             incoming(
                 ids::general_relativity,
                 TheoryRelationKind::LimitingTheory,
                 lp::gravitational_compactness,
                 vs::general_relativity_to_newtonian_gravity_limit),
             incoming(ids::newtonian_gravity, TheoryRelationKind::StructuralDependency),
         },
         .channels = channels({sc::mass_density_id}, {sc::effective_gravity_id}),
         .conserved = {cq::linear_momentum, cq::energy},
         .validity = vd::weak_field_slow_motion,
         .bridges = {mb::relativistic_to_effective_gravity},
         .boundaries = {br::gravitational_domain},
         .status = ImplementationStatus::Implemented,
         .epistemic = EpistemicStatus::EffectiveEstablished});

    std::vector<ModelBridgeDescriptor> bridges{
        ModelBridgeDescriptor{
            .id = mb::relativistic_to_newtonian_particles,
            .name = "RelativisticToNewtonianParticles",
            .description = "Uses the same canonical (position, momentum, rest mass) state in the beta -> 0 regime.",
            .source = ids::relativistic_particle_mechanics,
            .target = ids::newtonian_particle_dynamics,
            .kind = ModelBridgeKind::SharedCanonicalState,
            .preserves = {cq::mass, cq::linear_momentum, cq::angular_momentum},
        },
        ModelBridgeDescriptor{
            .id = mb::relativistic_to_effective_gravity,
            .name = "RelativisticToEffectiveGravity",
            .description = "Projects weak, slowly varying spacetime geometry into a Newtonian acceleration field.",
            .source = ids::general_relativity,
            .target = ids::effective_newtonian_gravity_field,
            .kind = ModelBridgeKind::Projection,
            .preserves = {},
        },
        ModelBridgeDescriptor{
            .id = mb::statistical_to_thermodynamic,
            .name = "StatisticalToThermodynamic",
            .description = "Projects equilibrium ensemble statistics into thermodynamic state variables.",
            .source = ids::statistical_mechanics,
            .target = ids::thermodynamics,
            .kind = ModelBridgeKind::Projection,
            .preserves = {cq::mass, cq::particle_number, cq::energy},
        },
        ModelBridgeDescriptor{
            .id = mb::kinetic_to_hydrodynamic,
            .name = "KineticToHydrodynamic",
            .description = "Takes distribution moments while preserving conservative continuum totals.",
            .source = ids::kinetic_theory,
            .target = ids::hydrodynamics,
            .kind = ModelBridgeKind::Projection,
            .preserves = {cq::mass, cq::linear_momentum, cq::energy},
        },
        ModelBridgeDescriptor{
            .id = mb::plasma_to_magnetohydrodynamic,
            .name = "PlasmaToMagnetohydrodynamic",
            .description = "Coarse-grains plasma kinetics into conducting-fluid and magnetic-field moments.",
            .source = ids::plasma_physics,
            .target = ids::magnetohydrodynamics,
            .kind = ModelBridgeKind::Projection,
            .preserves = {
                cq::mass,
                cq::linear_momentum,
                cq::energy,
                cq::electric_charge,
                cq::magnetic_flux,
            },
        },
    };

    return TheoryRegistry{std::move(theories), std::move(bridges)};
}

}  // namespace principia::ontology
