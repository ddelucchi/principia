#include <principia/scheduler/solver_contract.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

[[nodiscard]] principia::scheduler::ErrorEstimate zero_error()
{
    return {};
}

void test_error_estimates_fail_closed()
{
    using principia::scheduler::ErrorEstimate;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const ErrorEstimate hidden_nan{0.0, nan, 0.0, 0.0, 0.0};
    require(!hidden_nan.valid() && std::isinf(hidden_nan.numerical()),
            "a NaN after the first max operand must not look like zero numerical error");

    const ErrorEstimate model_nan{0.0, 0.0, 0.0, 0.0, nan};
    require(!model_nan.valid() && std::isinf(model_nan.total()),
            "invalid model error must make aggregate error fail closed");

    const ErrorEstimate ordinary{0.1, 0.2, 0.3, 0.4, 0.5};
    require(ordinary.valid() && ordinary.numerical() == 0.4 && ordinary.total() == 0.5,
            "finite normalized estimates should retain max-norm semantics");
}

void test_residual_reports_are_well_formed()
{
    using namespace principia;
    scheduler::ConstraintReport constraints{
        {{ontology::constraints::fixed_kinematic, 0.25, 1.0}},
        true,
        true,
    };
    require(constraints.well_formed() && constraints.satisfied() &&
                constraints.worst_normalized_residual() == 0.25,
            "a finite, identified constraint report within tolerance should pass");

    constraints.residuals.push_back(constraints.residuals.front());
    require(!constraints.well_formed() && !constraints.satisfied() &&
                std::isinf(constraints.worst_normalized_residual()),
            "duplicate residual identities must not double-count one constraint");

    constraints = scheduler::ConstraintReport{{}, true, true};
    require(!constraints.well_formed(), "projection cannot be claimed without naming a projected constraint");

    constraints = scheduler::ConstraintReport{
        {{ontology::ConstraintId{}, 0.0, 1.0}},
        true,
        false,
    };
    require(!constraints.well_formed(), "reserved residual identity 0 must be rejected");

    constraints = scheduler::ConstraintReport{
        {{ontology::constraints::fixed_kinematic, 0.0, 1.0}},
        false,
        false,
    };
    require(!constraints.well_formed(), "an unevaluated report cannot contain evaluated residual data");

    const scheduler::ConstraintReport explicitly_no_constraints{{}, true, false};
    require(explicitly_no_constraints.well_formed() && explicitly_no_constraints.satisfied(),
            "an evaluated empty constraint set should explicitly mean no constraints apply");

    scheduler::ConservationReport conservation{
        {{ontology::conserved_quantities::mass, 0.0, 1.0e-12}},
        true,
        true,
    };
    require(conservation.well_formed() && conservation.satisfied(),
            "a source-complete conservation residual within tolerance should pass");
    conservation.residuals.front().tolerance = std::numeric_limits<double>::infinity();
    require(!conservation.well_formed() && !conservation.satisfied(),
            "infinite tolerances must not make conservation vacuously true");

    const scheduler::ConservationReport empty_conservation{{}, true, true};
    require(empty_conservation.well_formed() && !empty_conservation.satisfied(),
            "conservation cannot pass without naming any audited quantity");
}

void test_capability_and_execution_contract_validation()
{
    using namespace principia;
    const scheduler::ConstraintCapability opaque_projection{
        ontology::constraints::fixed_kinematic,
        scheduler::ConstraintCapabilityKind::Projects,
    };
    require(!opaque_projection.valid(), "constraint projection must include residual evaluation capability");
    const scheduler::ConstraintCapability audited_projection{
        ontology::constraints::fixed_kinematic,
        scheduler::ConstraintCapabilityKind::Evaluates | scheduler::ConstraintCapabilityKind::Projects,
    };
    require(audited_projection.valid(), "audited projection should be a valid constraint capability");

    scheduler::SolverExecutionContract contract{
        ontology::theory_ids::newtonian_particle_dynamics,
        scheduler::ValidityContractRevision{1},
        scheduler::StepContractRevision{1},
        scheduler::IntegratorContractRevision{1},
        scheduler::StepControlPolicy::Fixed,
        scheduler::IntegratorFamily::Symplectic,
        {ontology::conserved_quantities::mass},
        {audited_projection},
        {},
        scheduler::DeterministicNumericPolicy{
            scheduler::DeterminismGuarantee::BitwiseWithinBuild,
            scheduler::FloatingPointPolicy::StrictIeee754,
            scheduler::NonFinitePolicy::RejectProposal,
            scheduler::IterationOrderPolicy::CanonicalPersistentId,
            scheduler::ParallelAccumulationPolicy::SerialCanonical,
        },
    };
    require(contract.complete(), "a fully specified, internally consistent solver contract should be complete");
    contract.claimed_conserved_quantities.push_back(ontology::conserved_quantities::mass);
    require(!contract.complete(), "duplicate conservation claims must make a direct contract incomplete");

    auto tolerance_policy = contract.numeric;
    tolerance_policy.guarantee = scheduler::DeterminismGuarantee::DeterministicWithinTolerance;
    require(!tolerance_policy.complete(), "a tolerance-based guarantee must name a positive tolerance");
    tolerance_policy.normalized_reproducibility_tolerance = 1.0e-12;
    require(tolerance_policy.complete(), "a finite positive reproducibility tolerance should complete that policy");
    tolerance_policy.guarantee = scheduler::DeterminismGuarantee::BitwiseWithinBuild;
    require(!tolerance_policy.complete(), "bitwise guarantees must not hide a nonzero tolerance");
}

void test_proposal_validation()
{
    using namespace principia;
    scheduler::SolverProposal proposal{
        units::seconds(1.0),
        units::seconds(0.5),
        zero_error(),
        {},
        {},
        1,
    };
    require(proposal.valid(), "a finite preferred step below the stable bound should be a valid proposal");

    proposal.preferred_step = units::seconds(2.0);
    require(!proposal.valid(), "preferred step must not exceed the declared stability bound");
    proposal.preferred_step = units::seconds(0.5);
    proposal.requested_substeps = 0;
    require(!proposal.valid(), "a proposal must request at least one substep");
    proposal.requested_substeps = 1;
    proposal.neighboring_data = scheduler::NeighborDataRequirement{
        0,
        state::ChannelSet{state::standard_channels::position_id},
    };
    require(!proposal.valid(), "neighbor channels without positive halo depth are contradictory");
    proposal.neighboring_data = scheduler::NeighborDataRequirement{1, {}};
    require(!proposal.valid(), "positive halo depth without neighbor channels is contradictory");
    proposal.neighboring_data = {};
    proposal.required_boundary_channels = state::ChannelSet{state::StateChannelId{}};
    require(!proposal.valid(), "boundary requirements cannot contain reserved channel id 0");
}

void test_step_result_transaction_semantics()
{
    using namespace principia;
    scheduler::StepResult accepted{
        scheduler::StepDisposition::Accepted,
        spacetime::SimulationTime{units::seconds(1.0)},
        units::seconds(0.1),
        1,
        zero_error(),
        scheduler::ConstraintReport{{}, true, false},
        {},
    };
    require(accepted.valid(), "an accepted finite transactional advance should be valid");
    accepted.advanced = units::seconds(0.0);
    require(!accepted.valid(), "an accepted result must advance positive simulation time");

    scheduler::StepResult rejected{
        scheduler::StepDisposition::Rejected,
        spacetime::SimulationTime{units::seconds(1.0)},
        units::seconds(0.0),
        1,
        zero_error(),
        {},
        {},
    };
    require(rejected.valid(), "a rejected transaction may report work but must commit zero time");
    rejected.advanced = units::seconds(0.01);
    require(!rejected.valid(), "a rejected transaction must not expose a partial committed advance");

    accepted.advanced = units::seconds(0.1);
    accepted.conservation = scheduler::ConservationReport{
        {{ontology::conserved_quantities::mass, 2.0, 1.0}},
        true,
        true,
    };
    require(!accepted.valid(), "an accepted step cannot carry an evaluated failed conservation audit");

    accepted.conservation = {};
    accepted.ending_time = spacetime::SimulationTime{units::seconds(-1.0)};
    require(!accepted.valid(), "simulation time in a step result must remain finite and nonnegative");
}

}  // namespace

int main()
{
    test_error_estimates_fail_closed();
    test_residual_reports_are_well_formed();
    test_capability_and_execution_contract_validation();
    test_proposal_validation();
    test_step_result_transaction_semantics();
}
