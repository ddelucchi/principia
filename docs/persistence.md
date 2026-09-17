# Persistence contracts

Principia currently has three persistence contracts with deliberately different purposes. Snapshot V1 is a bounded state projection, Replay V1 is that projection plus a scheduled command stream, and Foundation Checkpoint V2 is the complete in-memory resume image for the current foundation simulation bundle. They are not interchangeable save-file names for the same data.

The public DTOs and error contracts are defined by [snapshot.hpp](../reality/serialization/include/principia/serialization/snapshot.hpp), [replay.hpp](../reality/replay/include/principia/replay/replay.hpp), and [checkpoint.hpp](../reality/serialization/include/principia/serialization/checkpoint.hpp). Those headers and their encoders/decoders are authoritative when this document and the code disagree.

## Which contract to use

| Contract | Intended use | Canonical wire format | Runtime compatibility gate | Complete resume? |
| --- | --- | --- | --- | --- |
| Snapshot V1 | Stable, compact projection of particle and gravity-operator state | Text, schema `1` | Schema and value validation only | No |
| Replay V1 | Re-execute a bounded command stream from a Snapshot V1 projection | Text, schema `1`, with a byte-counted Snapshot V1 payload | Schema, command validation, decode limits, and execution budgets; no solver identity check | No |
| Foundation Checkpoint V2 | Resume every resource in the current foundation particle/gravity/contact simulation bundle | Canonical little-endian binary, schema `2` | Exact simulation-contract match, current semantic RNG mixer, understood registry sections, and full resource validation | Yes, for the current foundation bundle |

"Canonical" describes the encoder output: semantically equivalent accepted DTOs produce the same ordering and normalized representation. It does not mean that every decoder requires its input to have already been emitted in canonical order. Every decoder validates the resulting DTO and rejects trailing data.

## Snapshot V1

### Scope

`SnapshotV1` captures:

- world tick and elapsed simulation time;
- the particle-store revision and each particle's persistent ID, position, momentum, rest mass, material ID, and free/fixed constraint;
- the gravity composition policy and gravity-operator definitions.

It does not capture the gravity-operator graph revision, the base gravity field, boundaries, material definitions or catalog capabilities, mechanical-response definitions, disc colliders, identifier allocators, the world seed or semantic RNG mixer, chunks, a solver/integrator contract, or game and presentation state. A material ID in a particle record is therefore an identifier only; Snapshot V1 does not provide the definition needed to resolve it.

There is intentionally no general `restore_snapshot_v1` API. Replay V1 provides its own bounded reconstruction path for the resources it knows how to supply, but Snapshot V1 alone is not a complete world image.

### Canonical representation and validation

The text begins with `principia.snapshot 1`. Integer fields are decimal. Floating-point fields are canonical SI scalars emitted with `std::to_chars`, general format, and `max_digits10`. Negative zero is normalized to positive zero. Particles are ordered by particle ID; gravity operators are ordered by priority and then operator ID; quarter-turn rotations are reduced to the range `[0, 3]`.

Validation rejects unsupported schemas, malformed or duplicate IDs, non-finite values, negative simulation time, non-positive mass, momentum on fixed particles, invalid radii or gravity transforms, exhausted or unreachable particle revisions, invalid counts, truncation, and trailing tokens. The default decode limits are 64 MiB of input, 1,000,000 particles, and 1,000,000 gravity operators. Callers may provide stricter limits.

### Determinism boundary

Snapshot V1 can provide a byte-stable representation of its projection. It cannot by itself reproduce a simulation: the base field, referenced registries, allocator state, RNG semantics, and execution contract are absent. Byte equality of two snapshots means equality of the canonical V1 projection, not equality of every live world resource.

## Replay V1

### Scope and execution order

`ReplayV1` adds the following to an initial Snapshot V1:

- a constant two-component base-gravity vector;
- a positive fixed step and an exclusive end tick;
- commands to apply a particle impulse, install a gravity operator, or remove a gravity operator.

A command key is `(tick, sequence)`. Commands at tick `T` execute in sequence order immediately before the integration step from `T` to `T + 1`. Keys must be unique and command ticks must lie in `[initial_tick, end_tick)`. Validation also checks that addressed particles and operators exist at the relevant point in the command stream, that impulses target free particles, and that installed operators are valid for the selected composition policy.

Initial-state restoration reconstructs only the bounded `WorldState2` particle/clock projection and the gravity-operator graph. Replay execution owns that reconstructed state and returns an owned final state; it does not mutate caller-owned live state. The result can be explicitly projected back to Snapshot V1.

### Canonical representation, limits, and budgets

The text begins with `principia.replay 1`. It uses the same SI floating-point representation, negative-zero normalization, and quarter-turn normalization as Snapshot V1. Commands are ordered by `(tick, sequence)`. The initial snapshot is canonically encoded first and embedded as an exact byte-counted payload, so its internal newlines do not make framing ambiguous.

Default decode limits are 128 MiB of replay input, the nested Snapshot V1 limits, and 1,000,000 commands. Execution separately defaults to at most 10,000,000 integration steps and 1,000,000 commands. The command and step budgets are checked before reconstruction and stepping. In particular, the independent execution command budget prevents a DTO passed directly to `run_replay_v1` from bypassing the command-count bound; the other decode limits still apply only when decoding bytes.

### Determinism boundary

Replay V1 does not persist a theory ID, integrator ID or revision, step/validity revision, numeric ABI, fixed frame, contact registries, RNG contract, or allocator state. `run_replay_v1` uses the `NewtonianParticleSolver2` implementation compiled into the executing runtime. Consequently, Replay V1 can test repeatability under the same runtime implementation, but the format provides no check that a later runtime implements the same step semantics. A schema-1 replay accepted by two different builds is not, by that fact alone, guaranteed to produce the same trajectory.

## Foundation Checkpoint V2

### Complete foundation resume image

`FoundationCheckpointV2` captures and restores the resources required by the current foundation particle/gravity/contact simulation:

- world tick and elapsed simulation time;
- particle definitions and exact particle-store revision;
- validated boundary definitions, conditions, and exact boundary-registry revision;
- material definitions, exact material-registry revision, and material-catalog presence/known-family/identifier metadata;
- the constant effective-Newtonian base-gravity descriptor and vector;
- gravity composition policy, operator definitions, and exact graph revision;
- mechanical-response definitions and exact registry revision;
- particle-to-radius disc colliders and exact registry revision;
- world seed and semantic RNG mixer version;
- particle, boundary, and gravity-operator allocator cursors, including exhausted state;
- the simulation contract described below.

The material, mechanical-response, particle, and collider references are validated together. Boundary conditions, catalog references, gravity transform compatibility, persistent IDs, registry revision reachability, allocator cursors, finite values, dimensions, and physical scalar domains are also checked before a restored bundle is returned.

"Complete" is scoped to this foundation bundle. V2 does not claim to serialize arbitrary future registries, chunks, scenario logic, UI state, presentation caches, or other game-layer state unless they are added to the contract.

### Canonical binary representation

The binary stream begins with the bytes `principia.foundation.checkpoint` followed by the little-endian 32-bit schema value `2`. Integers have explicit widths and little-endian order. Doubles are stored as their IEEE-754 binary64 bits after negative-zero normalization. Strings and section payloads are length-delimited; no native C++ object layout or locale-dependent formatting is used.

Canonical ordering is by persistent key: particles and boundaries by ID, boundary conditions by channel, materials and mechanical responses by ID, catalog identifiers numerically, gravity operators by priority then ID, colliders by particle ID, and extension sections by type ID. Quarter turns are normalized modulo four. The checkpoint round-trip test proves both byte-stable repeated encoding and an encode/restore/capture/encode fixed point for the complete fixture.

### Runtime compatibility checks

Restore requires the caller to supply the contract of the selected runtime solver. Serialization does not guess an integrator or own a default solver identity. The supplied contract must be valid and must exactly equal every persisted `SimulationContractV2` field:

- theory ID;
- integrator ID and integrator revision;
- validity-contract and step-contract revisions;
- fixed frame and fixed-step duration;
- topology, physical-field, and spacetime dimensions;
- numeric ABI ID/revision and its scalar, byte-order, quantity, determinism, floating-point, non-finite, iteration-order, and accumulation policies.

The persisted semantic seed mixer version must also equal the runtime's `core::semantic_seed_mixer_version`. Mismatches produce explicit unsupported-contract or unsupported-mixer errors rather than a best-effort restore.

The current Newtonian contract is sourced from `NewtonianParticleSolver2::descriptor()` and solver constants. Its integrator is `NewtonianKickContactDriftKick`, integrator revision `3`, step-contract revision `3`, and numeric ABI `principia.ieee754.binary64.canonical-si` revision `1`. Revision 3 makes whole-system canonical frictionless contact, particle-pair impulse provenance, proposal-level contact events/energy flux, and contact configuration stamping part of authoritative step semantics. The serialization tests derive the theory and scheduler revisions from the descriptor rather than duplicating those identities in the serializer.

### Extension behavior

Mechanical-response and disc-collider registries are carried as required, typed, length-delimited sections. Their type IDs are `1` and `2`, and their current section revision is `1`. Both must be present exactly once. A decoder rejects a missing required section, a duplicate type ID, or an unsupported revision of either known section.

Other section type IDs are retained as opaque `(type ID, section revision, payload)` records and survive decode/re-encode. This makes the framing forward-readable. They are nevertheless required state: the current restore path rejects a checkpoint containing any unknown section. The rule is fail closed: an older runtime may inspect or preserve newer data, but it may not silently resume while discarding it. Opaque callers also may not use the reserved known type IDs.

### Decode limits

The default V2 limits are:

| Resource | Default maximum |
| --- | ---: |
| Whole input | 64 MiB |
| Cumulative ordinary string bytes | 1 MiB |
| Particles | 1,000,000 |
| Boundaries | 1,000,000 |
| Cumulative boundary conditions | 4,000,000 |
| Materials | 1,000,000 |
| Cumulative catalog entries | 4,000,000 |
| Gravity operators | 1,000,000 |
| Mechanical responses | 1,000,000 |
| Disc colliders | 1,000,000 |
| Extension sections | 1,024 |
| Cumulative extension payload bytes | 64 MiB |

Counts and lengths are checked before the corresponding reserve or allocation. The decoder rejects every strict prefix of a valid tested checkpoint as truncated and rejects any trailing byte. Limits are caller-supplied policy and may be lowered for a particular trust boundary.

### Atomic restore and determinism boundary

V2 restore is transactional in memory. It canonicalizes and validates the DTO, checks compatibility, and constructs the material, mechanical-response, boundary, particle, collider, and gravity-operator resources in local objects. The caller receives `RestoredFoundationV2` only after all restores succeed. A failure leaves caller-owned live state untouched; the persistence-only registry restore APIs are independently tested to preserve their previous state on failure.

This atomicity is not filesystem crash atomicity. The encoder returns bytes; durable replacement, checksums, authentication, encryption, compression, and backup policy belong to the storage layer. None of the three contracts is a trust or confidentiality envelope.

V2 records the state and execution identity needed to reject a known-incompatible runtime. Its default numeric ABI declares `BitwiseWithinBuild`, not bitwise equivalence across arbitrary compilers, architectures, or builds. Canonical checkpoint bytes are deterministic for the same canonical DTO; future simulation results are bounded by the persisted numeric ABI and by the runtime correctly honoring the exact contract it advertises.

## Versioning and migration rules

The current APIs implement no automatic schema migrations. All three decoders require their exact schema value and return `UnsupportedSchemaVersion` for another value. Snapshot V1 and Replay V1 have closed text grammars rather than extension framing; unknown or trailing fields are errors. Therefore:

1. Do not reinterpret an existing field or enum value. Snapshot V1 explicitly makes its persistent enum discriminants permanent, and Replay V1 embeds and reuses that schema.
2. A breaking change to a core wire layout requires a new schema and an explicit decoder/migrator. Do not make a schema-1 or schema-2 decoder infer a new layout.
3. A changed execution algorithm that leaves the V2 layout intact must change the appropriate simulation-contract identity or revision. Existing V2 data remains parseable but is intentionally rejected by a runtime that does not implement that exact contract.
4. A semantic RNG mixing change must increment the mixer version. The current restore path rejects the new version until explicit support or migration exists.
5. A changed payload layout for a known V2 registry section requires a new section revision and decoder support. A new required registry receives a unique section type ID and remains fail-closed on runtimes that do not understand it.
6. Snapshot V1 cannot be mechanically promoted to a complete V2 checkpoint: the missing base field, registries, historical revisions, allocator cursors, RNG state, and simulation contract require an explicit policy and authoritative source. Replay V1 likewise is not a substitute source for all missing state.

Tests covering these promises are in [test_persistence_edge_cases.cpp](../tests/unit/test_persistence_edge_cases.cpp), [test_replay.cpp](../tests/unit/test_replay.cpp), and [test_checkpoint_v2.cpp](../tests/unit/test_checkpoint_v2.cpp).
