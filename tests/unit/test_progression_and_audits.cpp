#include "../test_support.hpp"

#include <principia/conservation/balance.hpp>
#include <principia/game/progression/learning_graph.hpp>
#include <principia/game/reality_test_001.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <concepts>
#include <cmath>
#include <limits>

namespace {

template <typename Quantity>
concept MassBalance = requires(principia::conservation::Balance<Quantity> balance) {
    { balance.residual() } -> std::same_as<principia::units::Mass>;
};

static_assert(MassBalance<principia::units::Mass>);
static_assert(!MassBalance<principia::units::Momentum>);

}  // namespace

void test_progression_and_audits()
{
    using namespace principia;
    using tests::require;
    using tests::require_near;

    const auto graph = game::progression::make_standard_learning_graph();
    require(graph.ordered_nodes().size() == 7, "the initial learning graph must expose the full first causal arc");

    auto opening = game::progression::make_opening_access(graph);
    require(opening.permits_any_variant(
                game::progression::intervention_targets::particle_position,
                game::progression::AccessKind::Observe),
            "opening knowledge should reveal ordinary position state");
    require(!opening.permits_any_variant(
                game::progression::intervention_targets::effective_gravity_field,
                game::progression::AccessKind::Observe),
            "an unknown field must still act physically without being perceived");
    const auto skipped = opening.unlock(graph, game::progression::learning_nodes::newtonian_model);
    require(!skipped && skipped.error() == game::progression::UnlockError::MissingPrerequisite,
            "knowledge cannot skip the measurement prerequisite");

    auto test_access = game::progression::make_reality_test_access(graph);
    require(test_access.permits_any_variant(
                game::progression::intervention_targets::effective_gravity_field,
                game::progression::AccessKind::Observe),
            "field instrumentation must grant perception of effective gravity");
    require(test_access.permits_exact({
                game::progression::intervention_targets::effective_gravity_field,
                game::progression::InterventionKind::Field,
                game::progression::AccessKind::Modify,
                game::progression::CausalDepth::Field,
                state::standard_channels::effective_gravity_id,
                ontology::theory_ids::effective_newtonian_gravity_field}),
            "the Reality Test profile must grant typed field intervention");
    const auto planned = test_access.unlock(graph, game::progression::learning_nodes::geometry_unification);
    require(!planned && planned.error() == game::progression::UnlockError::PlannedNode,
            "metadata for deeper physics must not silently enable it");

    auto scenario = game::make_reality_test_001();
    const solvers::NewtonianParticleSolver2 solver;
    const solvers::NewtonianContactEnvironment2 contact{
        scenario.materials,
        scenario.mechanical_responses,
        scenario.disc_colliders,
    };
    const auto proposal = solver.propose(
        scenario.world,
        scenario.gravity,
        contact,
        units::seconds(1.0 / 120.0));
    require(proposal.has_value(), "the conservation fixture should produce a proposal");
    require(proposal->conservation.mass_status == conservation::AuditStatus::Closed,
            "particle mass must be audited");
    require(proposal->conservation.linear_momentum_status == conservation::AuditStatus::BalancedWithSources,
            "momentum must be balanced against external impulses");
    require(proposal->conservation.total_mechanical_energy_status == conservation::AuditStatus::MissingRepresentation,
            "operated gravity must not claim energy conservation without a potential/work contract");
    require(proposal->conservation.within(), "typed mass, momentum, angular momentum, and kinetic-energy ledgers must close");
    require_near(proposal->diagnostic.error.conservation_residual, 0.0, 1.0e-12,
                 "the normalized conservation diagnostic should reflect the typed ledger");

    auto failed_audit = proposal->conservation;
    failed_audit.mass_status = conservation::AuditStatus::Failed;
    require(!failed_audit.within(),
            "a numerically small residual must not override an explicit failed audit status");

    auto invalid_tolerance = conservation::ParticleBalanceTolerance2{};
    invalid_tolerance.relative = std::numeric_limits<double>::infinity();
    require(!invalid_tolerance.valid() && !proposal->conservation.within(invalid_tolerance) &&
                std::isinf(proposal->conservation.maximum_normalized_residual(invalid_tolerance)),
            "non-finite tolerances must fail closed instead of making every balance pass");

    auto nonfinite_audit = proposal->conservation;
    nonfinite_audit.kinetic_energy.after = units::joules(std::numeric_limits<double>::quiet_NaN());
    require(std::isinf(nonfinite_audit.maximum_normalized_residual()) && !nonfinite_audit.within(),
            "a non-finite residual in a later ledger must not be hidden by an earlier finite maximum");

    const auto* fixed_source = scenario.world.particles.find(world::ParticleId{2});
    require(fixed_source != nullptr, "the rock fixture must exist");
    auto fixed = *fixed_source;
    fixed.constraint = world::KinematicConstraint::Fixed;
    require(scenario.world.particles.replace(fixed).has_value(), "a valid fixed constraint should install transactionally");
    const auto constrained = solver.propose(
        scenario.world,
        scenario.gravity,
        units::seconds(1.0 / 120.0));
    require(constrained.has_value() && constrained->conservation.within(),
            "a fixed particle must close momentum through a recorded reaction impulse");
    const auto& rock_update = constrained->updates[1];
    require(rock_update.momentum == math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(0.0), units::kilogram_metres_per_second(0.0)},
            "a fixed particle must have zero canonical momentum");
    require(constrained->diagnostic.constraints.satisfied && constrained->diagnostic.constraints.projected,
            "the fixed constraint must expose an executable residual and its support reaction");

    world::ParticleSnapshot2 fast_snapshot = scenario.world.particles.snapshot();
    fast_snapshot.particles.front().momentum[0] = units::kilogram_metres_per_second(
        80.0 * 0.02 * 299'792'458.0);
    const solvers::NewtonianValidityEvaluator2 validity;
    const auto invalid = validity.evaluate(fast_snapshot);
    require(!invalid.valid && invalid.dominant_violation == diagnostics::ValidityReasonCode::SpeedRatioExceeded,
            "Newtonian validity must be executable rather than descriptive metadata");
    require(invalid.estimated_model_error > 0.0, "invalidity must retain a separate model-error estimate");

    fast_snapshot.particles.front().momentum[0] =
        units::kilogram_metres_per_second(std::numeric_limits<double>::quiet_NaN());
    const auto unsupported = validity.evaluate(fast_snapshot);
    require(!unsupported.valid && unsupported.dominant_violation == diagnostics::ValidityReasonCode::UnsupportedState,
            "non-finite canonical state must be rejected instead of disappearing from validity statistics");

    bool nan_rejected = false;
    try {
        require_near(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, "NaN must fail numeric assertions");
    } catch (const std::exception&) {
        nan_rejected = true;
    }
    require(nan_rejected, "numeric test support must never accept NaN as near a finite value");
}
