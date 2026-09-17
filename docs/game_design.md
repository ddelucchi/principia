# Game Design Constitution

## The promise

Principia is a 2D open-world action sandbox in which physical causality becomes
progressively observable, understandable, engineerable, and manipulable.

The player does not primarily become powerful by accumulating larger numerical
outputs. They gain access to variables farther upstream in the machinery that
produces an outcome:

```text
objects -> state -> sources -> fields -> material response -> boundaries
        -> couplings -> geometry -> symmetries -> parameters -> law operators
```

At the beginning, the world imposes conditions on the player. In the mature
endgame, the player designs local conditions under which the world evolves. The
game must remain tactile, dangerous, beautiful, and understandable throughout
that inversion; it must never collapse into either a reskinned spreadsheet or a
sequence of arbitrary magic effects.

## The opening feeling

The opening hours should feel like an exceptionally responsive pixel-art
sandbox before they feel like a physics course. Movement has weight. Materials
have recognizable behaviors. Weather, vegetation, loose objects, machines, and
creatures all appear to belong to the same world. The first surprising physical
phenomenon is experienced directly before an equation or graph explains it.

Early tools are concrete: a hammer, rope, lamp, pump, crude detector, simple
machine, or anomalous artifact. The player can enjoy them through feel and
experimentation alone. Instrumentation gradually reveals that familiar-looking
events share deeper causes.

## The durable player loop

Every major branch of progress follows the same epistemic loop:

```text
phenomenon -> measurement -> model -> prediction
           -> engineering -> manipulation -> unification
```

- A phenomenon creates curiosity or a practical problem.
- An instrument makes hidden state legible.
- Repeated measurements support a model.
- The model makes a prediction the player can test.
- A reliable prediction becomes a machine, technique, or material process.
- Mastery grants a typed intervention at the corresponding causal depth.
- Separate models eventually unify, opening a more general intervention.

Discovery must have mechanical value even when the human player already knows
the real-world science. Progress records demonstrated in-world capability, not a
quiz answer and not a character-level integer.

## One truth and five views

There is one committed physical universe. Five views of it must remain distinct:

1. **Physical:** canonical state, fields, materials, boundaries, operators, and
   consequences that are actually true.
2. **Effective:** the approximation, resolution, validity domain, and numerical
   error used to evolve a region affordably.
3. **Perceived:** what current senses and instruments allow the player to observe.
4. **Manipulable:** which typed causes current knowledge and technology authorize.
5. **Rendered:** the visual and acoustic projection used to communicate the other
   views.

An unknown field still moves a novice. Unlocking knowledge changes perception or
authority; it never switches the underlying cause on. Rendering observes
committed state and cannot perturb it.

## Three simultaneous literacy levels

Every important system should support three valid readings:

- A casual player sees that a strange crystal pulls a metal tool.
- A systems player recognizes a reusable launcher or transport mechanism.
- A physics-literate player recognizes a constitutive or field interaction and
  composes it with other systems.

The equations explain behavior and enable precision. They are never a prerequisite
for basic delight. Tooltips begin with consequences and controls, then reveal
measurements, models, uncertainty, and formalism as the player earns instruments.

## Non-negotiable causal rules

- Objects, terrain, creatures, projectiles, machines, and bosses respond through
  shared state, fields, material laws, boundaries, and interactions.
- Physics code contains no item-, creature-, or boss-specific consequence branch.
- An ability requests a typed cause. It never directly scripts damage, movement,
  terrain destruction, or a target reaction that should emerge downstream.
- A gravity operator modifies the shared gravity field; sand, smoke, water,
  projectiles, and bodies consult that same field independently.
- State, governing law, and constitutive material response are different APIs and
  different tiers of player power.
- Deep crafting validates functioning physical configurations. Recipes may teach
  early constructions but cannot replace causal validation.
- Arbitrary callbacks are not player-created laws. Endgame law manipulation uses
  a dimensionally typed, validated, serializable operator language.
- Approximation is welcome; dishonest attribution is not. Missing physics and
  validity limits remain visible rather than being disguised as exact simulation.

## World, persistence, and failure

Principia is a persistent-world sandbox, not a disposable effect stage. If a
mountain moves, the mountain remains moved. Water finds a new route, structures
lose support, resources become exposed, and settlements inherit the consequence.

Failure should produce stories rather than only a binary defeat screen: a vessel
ruptures, a resonant machine tears itself apart, a coolant loop fails, a field
experiment isolates a cavern, or a large intervention changes an ecosystem.
Deterministic commands, complete checkpoints, provenance, and reproducible world
generation are therefore game-design requirements, not merely engineering
conveniences.

## Progression and content

The learning graph is separate from the physics ontology. Physics records how
models relate; progression records the experiences, instruments, evidence, and
engineering demonstrations through which this player gains access.

Artifacts are valuable because they embody operators, boundaries, couplers, or
material responses. An object first understood as a "gravity rotator" may later
be recognized as a general vector rotation and reused on another compatible
field. Its conceptual meaning expands without changing its underlying identity.

Bosses and major hazards are physical systems with observable constraints,
sources, transport paths, stability regimes, and failure modes. Their phases
emerge from changing state or boundary conditions, not invulnerability flags.

Civilizations can embody different points on the causal ladder. Mechanical,
thermal, electromagnetic, field-engineering, and geometric cultures communicate
their knowledge through infrastructure and environmental behavior. Lore should
reveal a history of increasingly upstream access to reality rather than sit apart
from gameplay as unrelated prose.

Established physics, controlled approximation, speculative physics, and fictional
extension must be visibly distinguished. The game may become fantastical beyond
the frontier of known physics, but it should be honest about when it crosses it.

## Visual language

Pixel art is not decoration placed over the simulation. It is an instrument for
making physical state legible.

- Motion communicates inertia, momentum, drag, and constraint.
- Materials bend, crack, glow, condense, align, wet, freeze, melt, and fail before
  a label announces the change.
- Rain, smoke, vegetation, dust, loose debris, flame, and liquid surfaces reveal
  fields and flows shared with gameplay objects.
- A change to gravity reorients every downstream visual reference to gravity.
- A change to propagation, optics, or geometry alters the visual grammar itself,
  not merely the color of an aura.
- Instrument overlays add vectors, contours, fluxes, spectra, residuals,
  uncertainty, and validity boundaries only when the player has earned that view.

Two dimensions are an advantage: wavefronts, cracks, currents, field vectors,
temperature, pressure, and causal relationships can remain readable at world
scale. The desired contrast is a tiny expressive character standing beside a
consequence vast enough to reshape a landscape.

## Sound and interface

Sound carries physical information. Resonant structures sing; electrical systems
hum; stress creaks; unstable machinery changes spectrum; propagation anomalies
sound spatially wrong. An experienced player should sometimes diagnose a machine
by ear.

The early interface remains spare and physical. Later instruments progressively
unlock thermal, stress, field, spectrum, momentum, energy-flow, and causality
views. Player instruments and developer inspectors should share read-only query
and diagnostic infrastructure, with progression controlling which projection is
available in play.

## Current executable proof

Reality Test 001 contains a player, rock, sand grain, one operated gravity field,
typed materials and mechanical responses, disc collision geometry, and a
multi-channel static wall. All entities sample the same rotated gravity field.
The SDL presentation reads the shared inspection model, can reveal the field, and
installs or removes the same serialized operator under a typed causal permission.

Reality Test 002 validates swept frictionless contact, restitution, momentum
exchange, explicit unresolved-energy flux, collision provenance, and
transactional source stamps. These tests prove a foundation seam; they are not a
claim that general rigid bodies, terrain, combat, or the eventual art direction
already exist.

## Development gates

New domains enter in causal order. A new solver is not "implemented" until it has:

- canonical state and typed units;
- explicit reads, writes, materials, boundaries, and source revisions;
- validity and fallback policy;
- deterministic step and numeric contracts;
- convergence or analytic fixtures;
- executable constraint and conservation accounting;
- complete persistence/replay implications;
- read-only inspection and a legible presentation path;
- adversarial rejection tests for malformed, stale, and non-finite inputs.

Thermal/phase systems, continuum fluids, electromagnetism, relativity, curved
spacetime, and selected quantum systems should be added as coherent effective
models in that order of readiness. Ontology metadata may describe later theories;
metadata must never masquerade as an implemented solver.

## Acceptance question

For every feature, ask:

> Did the player obtain a new scripted effect, or did they gain meaningful access
> to a cause that already governs the shared world?

The second answer is the organizing invariant of Principia.
