# Architecture

## Prime directive

Principia is a Reality Engine with a 2D game downstream of it. A gameplay result
must arise from shared physical causes whenever the engine represents those
causes. The architecture knows the long dependency ladder from the beginning,
but implements only the cheapest coherent effective model that has passed its
validation gates.

That produces two equally important rules:

- ontology metadata for a future theory is not an implementation claim;
- a working foundation slice must not be bypassed by object-specific gameplay
  consequences.

## Current milestone boundary

The executable repository contains the substrate, Reality Test 001, Reality Test
002, and their foundation validation surfaces:

- dimensionally typed quantities, coordinates, frames, channels, fields, and
  canonical particle state;
- a complete descriptive theory ontology with explicit epistemic and
  implementation status;
- a revisioned gravity-operator IR over one effective gravity field;
- transactional Newtonian particle dynamics with fixed constraints, swept disc
  contact, material restitution, validity checks, and step-doubling diagnostics;
- typed conservation ledgers with sources and outward fluxes;
- a validated numerical scheduler contract and deterministic dependency graph;
- exact causal-access permissions on a separate learning graph;
- partial Snapshot/Replay V1 formats and a complete Foundation Checkpoint V2;
- one read-only inspection model used by tools and presentation;
- a downstream SDL3/SDL_GPU pixel presentation.

Thermal, phase, continuum-fluid, electromagnetic, relativistic, curved-spacetime,
and quantum solvers do not exist yet. Their ontology nodes are descriptive only.

## Dependency direction

```text
core / units / math
        |
        v
spacetime / state / fields / ontology / materials / boundaries
        |
        v
operators / world / conservation / diagnostics / scheduler / solvers
        |
        v
serialization / replay / game content
        |
        v
tools and presentation
```

The exact CMake graph is smaller and more detailed than this conceptual diagram,
but it obeys the same direction. Physics targets never link game or presentation.
Presentation reads committed simulation state and cannot mutate physical truth.
Build-only warnings and sanitizer policy do not leak through the installed target
package.

## Five different graphs

The repository deliberately keeps these meanings separate:

1. **Physics ontology:** structural dependencies, limits, effective models,
   couplings, coarse graining, and unknown completions.
2. **Numerical execution:** concrete channel reads/writes, conflicts, reductions,
   strongly connected solver groups, and deterministic order.
3. **Software dependencies:** CMake target includes and links.
4. **Model resolution:** microscopic, particle, mesoscopic, continuum, and
   aggregate representations chosen per domain.
5. **Learning/progression:** experiences and prerequisites that grant exact
   observation or intervention permissions.

Shared persistent IDs may connect these graphs. No universal "physics graph" may
erase their distinct semantics.

## Canonical state and controlled mutation

A particle stores only persistent identity, position, momentum, rest mass,
material identity, and a kinematic constraint. Velocity and kinetic energy are
derived. Every insertion, replacement, impulse, bulk kinematic update, and
persistence restore validates identifiers, finiteness, positive mass,
constraints, complete coverage, duplicate IDs, and reachable revision state
before changing the live store.

The same pattern applies to boundaries, materials, mechanical responses,
colliders, gravity operators, and progression metadata: live containers expose
const iteration and controlled revisioned mutations. Restore operations build and
validate a replacement off to the side, then commit it atomically.

Derived render state, diagnostics, and serialized DTOs are not competing mutable
world copies.

## State, field, material, boundary, and law

These concepts are intentionally orthogonal:

- a state channel identifies a typed physical value and where it lives;
- a field maps an event to a typed value and publishes representation metadata;
- a material selects constitutive model identities;
- a boundary applies channel-specific conditions over explicit geometry;
- an operator changes a source, field, boundary, response, parameter, or law
  through a constrained typed IR.

Reality Test 001's gravity operator transforms a common gravity field in canonical
`(priority, persistent operator ID)` order. It contains no branch for player,
rock, sand, projectile, or any other consumer.

## Transactional mechanics

The implemented particle step is a strict, fixed-step kick/contact-drift/kick
operator split. In the point-particle limit it is velocity Verlet:

```text
p_(n+1/2) = p_n + (dt/2) m g(x_n, t_n)
x_(n+1)   = x_n + dt p_(n+1/2) / m
p_(n+1)   = p_(n+1/2) + (dt/2) m g(x_(n+1), t_(n+1))
```

Contact geometry is resolved during the drift rather than after penetration.
Rigid channel boundaries exchange explicit impulse. Frictionless material
response composes a coefficient of restitution, and lost represented kinetic
energy leaves through an explicit unresolved-internal-energy flux. Fixed
kinematic state is projected with a recorded reaction impulse.

`propose()` operates on a canonical snapshot and stamps every resource it reads:
particle, field, boundary, material, mechanical response, collider, clock, and
tick revisions as applicable, plus every contact event/body/boundary budget and
geometric/simultaneity tolerance. `commit()` rejects a stale or
context-mismatched stamp, recomputes the canonical proposal, compares all
physical updates, global events, fluxes, and ledgers, and applies one complete
bulk mutation. Forged, incomplete, duplicate, non-finite, stale,
constraint-failing, or conservation-failing work cannot partly commit.

One full step and two half steps provide a normalized integration-error estimate.
Analytic constant-acceleration and harmonic-oscillator fixtures test exactness and
second-order convergence rather than trusting the integrator label.

## Validity, constraints, and conservation

Numerical accuracy and model validity are separate reports. The Newtonian model
evaluates the maximum particle speed ratio `beta = |v|/c`, records the worst
persistent ID with deterministic tie-breaking, distinguishes near-limit from
outside-domain state, and rejects a step whose proposed endpoint has no fallback.

Fixed constraints publish per-particle position/velocity residuals and reaction
impulses. Conservation is expressed as:

```text
after = before + source_into_system - outward_flux
```

Mass, linear momentum, angular momentum, and kinetic energy use typed absolute
and relative tolerances that fail closed on malformed or non-finite values.
Gravity, supports, and rigid boundaries are attributed explicitly; restitution
loss is an outward energy flux. Total mechanical energy remains
`MissingRepresentation` because the effective gravity interface does not yet
provide a potential and operator-work contract. A numerically small residual
cannot override an explicit failed status.

## Scheduler contract

Every solver descriptor identifies its theory and typed validity, step, and
integrator revisions. It declares step control, integrator family, conservation
claims, constraint and boundary capabilities, non-finite policy, floating-point
policy, iteration order, accumulation order, and reproducibility guarantee.

The execution graph validates descriptors against registered theory/channel
metadata. Multiple writers require a typed, revisioned reducer with a defined
fold order. Ambiguous reducers, type mismatches, undeclared channels, malformed
reports, and incoherent accepted/rejected results fail graph construction or step
validation. Strongly connected components are built iteratively so a large graph
cannot exhaust the call stack.

## Determinism and provenance

Authoritative iteration uses persistent-ID order. Semantic random streams derive
from a versioned, domain-separated seed mixer rather than call order. Strict
floating-point options disable unsafe reassociation/contraction for simulation
targets. The current numeric claim is bitwise reproducibility within one build,
not an unproved cross-platform guarantee.

Render observations, field visualization, inspector queries, and wall-clock
diagnostic timing are downstream and excluded from physical proposal equality.
Every persistent identity, schema, numeric ABI, RNG mixer, solver contract, and
extension section has an explicit compatibility rule.

## Persistence and replay

The formats have different jobs:

- Snapshot V1 is a canonical, locale-independent partial DTO for clock,
  particles, and gravity-operator overlay.
- Replay V1 combines that partial initial state with ordered impulse/operator
  commands and a fixed point-particle step contract.
- Foundation Checkpoint V2 is the complete restorable foundation image: contract,
  numeric ABI, RNG identity, seed, allocators, clock, particles, boundaries,
  materials/catalogs, mechanical responses, colliders, base gravity, and operator
  graph.

All decoders apply caller-provided byte/count/string limits before allocation,
reject strict truncation and trailing data, and preserve structured causes.
Checkpoint restore verifies the exact runtime-supported simulation contract and
cross-registry foreign keys before constructing a new live state. See
[persistence.md](persistence.md) for the detailed contract.

## Progression, inspection, and presentation

Causal permission is an exact six-field value: target, intervention kind, access
kind, causal depth, channel, and theory. Exact checks are used for authority;
explicit wildcard-style queries exist only for discovery/UI. Learning graphs
validate all grants and prerequisites, cache a canonical ordering, and reject
cycles without recursive stack growth.

Each normalized learning graph also has a domain-separated SHA-256 content
fingerprint over every node, dependency, status, and typed grant field. A causal
access state remains unbound until its first successful unlock, then accepts
unlocks and completion queries only for that fingerprint; a foreign graph fails
with `GraphMismatch` before it can reuse equal numeric node IDs as prerequisites.
Completed-node IDs exposed for persistence or UI are therefore meaningful only
together with the state's graph fingerprint. Reconstructed graphs with identical
canonical content retain the same identity, while changed definitions require an
explicit progression migration.

The shared inspection frame is read-only. It publishes clock and source
revisions, field metadata, canonical particle state, derived velocity, sampled
gravity, boundaries, and optional material/contact metadata. The world inspector
and SDL presentation consume that same projection. Player-facing instruments can
therefore reuse developer infrastructure while progression limits which portions
are perceived.

The presentation owns SDL and GPU resources through RAII. Its event loop, CPU
canvas, world projection, and GPU upload/lifecycle are separate components. It
never supplies state back to the simulation.

## Explicit limits and next gate

The current contact law covers frictionless discs and rigid static axis-aligned
box boundaries. It is not a general rigid-body engine: rotation, friction,
stacking/contact manifolds, deformable solids, fixed-disc obstacles, and terrain
collision require new explicit models and validation. Particle-particle contact
must reject unsupported coupled simultaneous configurations rather than silently
select an order-biased answer.

The next domain should not begin merely because its ontology node exists. The
foundation must remain green under strict Debug and Release builds, persistence
fixed-point tests, package-consumer tests, deterministic replays, convergence
fixtures, and presentation smoke runs. Only then should the smallest coherent
thermal/phase slice enter through the same contracts.
