# Principia

Principia is the executable foundation of a **Reality Engine** for a 2D
open-world sandbox. Game objects observe and intervene in one shared physical
state; progress means gaining access to increasingly upstream causes rather than
receiving larger scripted effects.

The current foundation proves two reality tests:

- **Reality Test 001:** player, rock, and sand share one operated gravity field,
  typed materials, mechanical response, collision geometry, and a multi-channel
  rigid wall. The presentation can reveal the same field and toggle the same
  serialized gravity operator that drives simulation.
- **Reality Test 002:** swept frictionless disc contact prevents tunneling,
  applies material restitution, records canonical events and boundary/internal
  impulses, and transfers lost kinetic energy into an explicit unresolved-energy
  flux.

The supporting substrate includes:

- C++23 and `mp-units` physical quantities at API boundaries;
- canonical particle state `(position, momentum, rest mass)` and controlled,
  revisioned mutation;
- distinct ontology, execution, software, resolution, and learning graphs;
- a kick/contact-drift/kick Newtonian solver with executable validity,
  constraints, conservation, source stamps, and step-doubling error estimates;
- validated deterministic scheduler and exact causal-permission contracts;
- canonical Snapshot/Replay V1 plus complete atomic Foundation Checkpoint V2;
- shared read-only inspection data for tools and SDL3/SDL_GPU presentation;
- strict Debug/Release tests for malformed, stale, non-finite, truncation,
  convergence, conservation, determinism, and package-consumer cases.

Later thermal, phase, fluid, electromagnetic, relativistic, curved-spacetime,
and quantum models are ontology metadata only. They are not claimed as implemented.

## Build and test

Requirements are CMake 3.25+, a C++23 compiler, and Git for the default pinned
dependency fetch. On Windows with Visual Studio 2022:

```powershell
cmake --preset vs2022
cmake --build --preset debug
ctest --preset debug
```

Use the `release` build/test presets for optimized verification.

For a generator-neutral build without SDL presentation:

```console
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug
```

`headless-release` is the optimized equivalent. `headless-sanitize` enables ASan
and UBSan with supported GCC/Clang toolchains and ASan with MSVC. The installed
MSVC environment may require its separate AddressSanitizer runtime component.

Dependency revisions are pinned and fetched by default. Set
`PRINCIPIA_USE_SYSTEM_DEPENDENCIES=ON` to prefer compatible installed packages,
or `PRINCIPIA_FETCH_DEPENDENCIES=OFF` to require all dependencies to be supplied.
Authoritative simulation remains CPU-side; only the presentation target links
SDL3.

## Run

The headless inspector executes one second of Reality Test 001 and prints the
committed physical/constitutive view:

```powershell
build\headless-debug\tools\Debug\principia_inspect.exe
```

The interactive presentation is built by `vs2022`:

```powershell
build\vs2022\presentation\Debug\principia_demo.exe
```

Controls: `Space` pauses, `N` advances one tick while paused, `F` toggles the
field instrument, `G` removes or installs the gravity operator, `R` resets, and
`Escape` exits. `--frames N` runs a finite smoke test.

## Install as a CMake package

```powershell
cmake --install build\vs2022 --config Debug --prefix C:\path\to\principia
cmake --install build\vs2022 --config Release --prefix C:\path\to\principia
```

Installed consumers use `find_package(Principia 0.1 CONFIG REQUIRED)` and link
names such as `Principia::Game`, `Principia::Solvers`, or `Principia::Units`.
`mp-units` remains an explicit package dependency; developer warning and
sanitizer flags do not leak to consumers. A system-supplied `mp-units` must use
the same public feature configuration as this build (`std::format`,
natural units, contracts, and CRTP mode); the pinned dependency path supplies
that configuration automatically. MSVC Debug and non-Debug runtime ABIs are
incompatible, so a normal Visual Studio consumer needs both configurations
installed into the same prefix. A Release-only consumer may instead configure
with `CMAKE_CONFIGURATION_TYPES=Release` (or use a single-config Release
generator). `find_package(Principia)` rejects missing ABI-compatible artifacts
during configuration rather than allowing a later linker failure.

## Constitutions

- [Architecture](docs/architecture.md)
- [Game design](docs/game_design.md)
- [Persistence and replay](docs/persistence.md)

