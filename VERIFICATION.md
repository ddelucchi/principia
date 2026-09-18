# Verification

Principia separates software-contract verification from physical-model
validation. The current public foundation release has strong evidence for
determinism, persistence, solver contracts, and package consumption; it does
not claim experimental validation of every ontology entry.

## Evidence layers

### Deterministic state and replay

The replay and persistence tests require, among other things:

- canonical byte-stable replay encoding;
- deterministic topological command ordering;
- identical initial state plus identical commands producing byte-identical
  canonical final snapshots;
- a changed causal command changing the canonical final state;
- rejection of duplicate command keys, missing entities, non-finite values,
  and non-positive timesteps.

These are direct software contracts rather than statistical expectations.

### Checkpoint and persistence integrity

Checkpoint tests exercise versioned foundation state including:

- particle and world clocks;
- material/model registries;
- boundaries and colliders;
- gravity operator graphs;
- allocator cursors;
- solver/integrator contract metadata;
- execution configuration;
- numeric ABI data.

The intent is that persistent state is explicit enough to reject incompatible
or malformed restoration rather than silently inventing runtime defaults.

### Solver contracts and validity

Solver descriptors expose theory, integrator, validity, and step revisions.
Tests exercise contract compatibility and failure semantics. A state or solver
outside its declared validity domain is expected to fail explicitly rather than
quietly switching interpretation.

### Cross-platform and sanitizer configuration

The repository defines portable headless presets for Debug, Release, and
sanitizer builds. The hosted workflow is configured for:

- Ubuntu headless build + tests;
- Windows headless build + tests;
- ASan + UBSan on Ubuntu;
- a Release install followed by an external package-consumer build.

The account currently reports GitHub Actions startup failures before job
creation, so this document does **not** claim those hosted jobs are green.
The same preset commands remain the clone-local verification path.

### External package-consumer check

The package-consumer target configures a separate CMake project against an
installed Principia prefix. This catches a different failure class from
in-tree compilation: missing exported targets, include paths, package metadata,
or public transitive requirements.

## What these checks do not establish

Passing the software suite does not establish:

- experimental validation of every physical model;
- continuum-limit accuracy for future PDE/field solvers not yet implemented;
- correctness outside each solver's declared validity regime;
- safety certification or suitability for safety-critical use;
- equivalence between ontology metadata and executable physics.

The public release deliberately treats those as separate claims.

## Reproduce locally

Headless Debug:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug
```

Release:

```bash
cmake --preset headless-release
cmake --build --preset headless-release
ctest --preset headless-release
```

Sanitizers on supported compilers:

```bash
cmake --preset headless-sanitize
cmake --build --preset headless-sanitize
ctest --preset headless-sanitize
```
