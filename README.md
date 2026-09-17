# Principia

Principia is a deterministic C++23 physics runtime for a programmable 2D physical world. The project is built around explicit physical state, typed units, solver contracts, reproducible scheduling, conservation accounting, replay, and inspection.

## Status

The current repository is a foundation release, not a claim to implement a complete universe simulator.

Implemented and exercised today:

- canonical particle state with position, momentum, rest mass, material, and constraints
- `mp-units` physical quantities at public API boundaries
- gravity fields and a Newtonian particle solver
- swept frictionless disc contact against static boundaries and other particles
- material restitution and mechanical-response registries
- deterministic solver dependency graphs and coupled-component detection
- explicit validity, step-control, and numeric-policy contracts
- conservation and unresolved-energy accounting
- snapshot, replay, and checkpoint persistence
- progression and causal-permission graphs
- headless inspection tools
- SDL3/SDL_GPU presentation code for interactive reality tests

Thermal, fluid, electromagnetic, relativistic, curved-spacetime, and quantum entries in the ontology are extension metadata unless corresponding executable models are added. They are not presented here as implemented solvers.

## Reality tests

### Reality Test 001

A player, rock, and sand object share one authoritative gravity field, typed materials, collision geometry, and a rigid wall. The same field state can be inspected by tools and presentation code.

### Reality Test 002

Swept frictionless disc contact prevents simple tunneling, applies material restitution, records canonical contact events and impulses, and transfers unresolved kinetic-energy loss into an explicit flux channel.

## Architecture

The code is divided by physical responsibility rather than by presentation feature:

- `reality/core` - stable identifiers, schemas, deterministic primitives
- `reality/units` and `reality/math` - dimensional quantities and vectors
- `reality/state` and `reality/world` - authoritative state channels and world objects
- `reality/fields` and `reality/operators` - field definitions and law application
- `reality/scheduler` - dependency analysis, contracts, coupled solver components
- `reality/solvers` - executable dynamics and contact solvers
- `reality/conservation` - balance and unresolved-flux accounting
- `reality/serialization` and `reality/replay` - persistence and deterministic reconstruction
- `game` - progression and reality-test composition
- `presentation` - SDL presentation only; simulation authority remains CPU-side
- `tools` - theory-graph and world-inspection utilities

See [docs/architecture.md](docs/architecture.md), [docs/game_design.md](docs/game_design.md), and [docs/persistence.md](docs/persistence.md).

## Determinism and failure policy

Principia treats determinism as part of the simulation contract. Solver descriptors declare state access, theory identity, validity revision, integrator family, step-control policy, and numeric policy. Ambiguous write conflicts, invalid state, non-finite input, unsupported contact configurations, and persistence inconsistencies are rejected explicitly rather than silently repaired.

The test suite covers scheduler topology, solver contracts, persistence edge cases, replay, checkpointing, progression permissions, contact behavior, and the two foundation reality tests.

## Build and test

Requirements:

- CMake 3.25+
- a C++23 compiler
- Git for pinned dependency fetches

Portable headless Debug:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug
```

Portable headless Release:

```bash
cmake --preset headless-release
cmake --build --preset headless-release
ctest --preset headless-release
```

ASan/UBSan where supported:

```bash
cmake --preset headless-sanitize
cmake --build --preset headless-sanitize
ctest --preset headless-sanitize
```

On Visual Studio 2022, the `vs2022`, `debug`, and `release` presets build the SDL presentation as well.

## Inspect the world

After a headless Debug build:

```bash
./build/headless-debug/tools/principia_inspect
```

On Windows with the Visual Studio preset:

```powershell
build\vs2022\presentation\Debug\principia_demo.exe
```

Interactive controls are documented in the presentation source and original design notes.

## Package consumption

Principia installs CMake package targets such as `Principia::Game`, `Principia::Solvers`, and `Principia::Units`. The repository includes a package-consumer test so installation is treated as part of the public interface rather than an afterthought.

## Scope

This is research-oriented systems software. It is not a safety-critical simulator, a certified engineering analysis package, or evidence that ontology entries without executable solvers have been physically validated.

## License

Source is publicly viewable for portfolio and technical evaluation. See [LICENSE](LICENSE).
