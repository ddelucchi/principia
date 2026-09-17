#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/materials/material.hpp>
#include <principia/math/vector.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/units/quantity.hpp>

#include <cstdint>
#include <cmath>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace principia::world {

struct ParticleIdTag;
using ParticleId = core::StrongId<ParticleIdTag, std::uint64_t>;

enum class KinematicConstraint : std::uint8_t {
    Free,
    Fixed,
};

struct ParticleState2 {
    ParticleId id;
    spacetime::WorldPosition<2> position;
    math::Vector<2, units::Momentum> momentum;
    units::Mass rest_mass;
    materials::MaterialId material;
    KinematicConstraint constraint{KinematicConstraint::Free};

    friend bool operator==(const ParticleState2&, const ParticleState2&) = default;
};

struct ParticleSnapshot2 {
    std::uint64_t revision{};
    std::vector<ParticleState2> particles;

    friend bool operator==(const ParticleSnapshot2&, const ParticleSnapshot2&) = default;
};

enum class ParticleRestoreError : std::uint8_t {
    InvalidIdentifier,
    DuplicateIdentifier,
    NonFiniteState,
    NonPositiveMass,
    InvalidConstraint,
    FixedParticleHasMomentum,
    RevisionExhausted,
    InvalidRevision,
};

enum class ParticleMutationError : std::uint8_t {
    InvalidIdentifier,
    DuplicateIdentifier,
    MissingParticle,
    NonFiniteState,
    NonPositiveMass,
    InvalidConstraint,
    FixedParticleHasMomentum,
    ConstrainedParticle,
    StaleRevision,
    IncompleteUpdate,
    RevisionExhausted,
};

struct ParticleKinematicUpdate2 {
    ParticleId id;
    spacetime::WorldPosition<2> position;
    math::Vector<2, units::Momentum> momentum;
};

class ParticleStore2 {
public:
    [[nodiscard]] std::expected<void, ParticleMutationError> insert(ParticleState2 particle)
    {
        if (const auto validity = validate_particle(particle); !validity) {
            return std::unexpected(validity.error());
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ParticleMutationError::RevisionExhausted);
        }
        if (!particles_.try_emplace(particle.id, std::move(particle)).second) {
            return std::unexpected(ParticleMutationError::DuplicateIdentifier);
        }
        ++revision_;
        return {};
    }

    [[nodiscard]] const ParticleState2* find(ParticleId id) const noexcept
    {
        const auto found = particles_.find(id);
        return found == particles_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] std::expected<void, ParticleMutationError> replace(ParticleState2 particle)
    {
        if (const auto validity = validate_particle(particle); !validity) {
            return std::unexpected(validity.error());
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ParticleMutationError::RevisionExhausted);
        }
        const auto found = particles_.find(particle.id);
        if (found == particles_.end()) {
            return std::unexpected(ParticleMutationError::MissingParticle);
        }
        found->second = std::move(particle);
        ++revision_;
        return {};
    }

    [[nodiscard]] std::expected<void, ParticleMutationError> apply_impulse(
        ParticleId id,
        const math::Vector<2, units::Momentum>& impulse)
    {
        if (!finite_momentum(impulse)) {
            return std::unexpected(ParticleMutationError::NonFiniteState);
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ParticleMutationError::RevisionExhausted);
        }
        const auto found = particles_.find(id);
        if (found == particles_.end()) {
            return std::unexpected(ParticleMutationError::MissingParticle);
        }
        if (found->second.constraint != KinematicConstraint::Free) {
            return std::unexpected(ParticleMutationError::ConstrainedParticle);
        }
        const auto momentum = found->second.momentum + impulse;
        if (!finite_momentum(momentum)) {
            return std::unexpected(ParticleMutationError::NonFiniteState);
        }
        found->second.momentum = momentum;
        ++revision_;
        return {};
    }

    // Applies one solver-created kinematic update for every current particle.
    // Validation is completed against a copy before the canonical store changes.
    [[nodiscard]] std::expected<void, ParticleMutationError> apply_kinematic_updates(
        std::uint64_t expected_revision,
        std::span<const ParticleKinematicUpdate2> updates)
    {
        if (revision_ != expected_revision) {
            return std::unexpected(ParticleMutationError::StaleRevision);
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ParticleMutationError::RevisionExhausted);
        }
        if (updates.size() != particles_.size()) {
            return std::unexpected(ParticleMutationError::IncompleteUpdate);
        }

        auto updated = particles_;
        std::set<ParticleId> visited;
        for (const auto& update : updates) {
            if (!update.id) {
                return std::unexpected(ParticleMutationError::InvalidIdentifier);
            }
            if (!visited.insert(update.id).second) {
                return std::unexpected(ParticleMutationError::DuplicateIdentifier);
            }
            const auto found = updated.find(update.id);
            if (found == updated.end()) {
                return std::unexpected(ParticleMutationError::MissingParticle);
            }
            if (!finite_position(update.position) || !finite_momentum(update.momentum)) {
                return std::unexpected(ParticleMutationError::NonFiniteState);
            }
            if (found->second.constraint == KinematicConstraint::Fixed) {
                if (!zero_momentum(update.momentum)) {
                    return std::unexpected(ParticleMutationError::FixedParticleHasMomentum);
                }
                if (update.position != found->second.position) {
                    return std::unexpected(ParticleMutationError::InvalidConstraint);
                }
            }
            found->second.position = update.position;
            found->second.momentum = update.momentum;
        }

        particles_ = std::move(updated);
        ++revision_;
        return {};
    }

    [[nodiscard]] ParticleSnapshot2 snapshot() const
    {
        ParticleSnapshot2 result;
        result.revision = revision_;
        result.particles.reserve(particles_.size());
        for (const auto& [id, particle] : particles_) {
            static_cast<void>(id);
            result.particles.push_back(particle);
        }
        return result;
    }

    // Replaces the store atomically after validating a persistent snapshot.
    // This is intentionally the only API that may set a historical revision.
    [[nodiscard]] std::expected<void, ParticleRestoreError> restore(ParticleSnapshot2 snapshot)
    {
        if (snapshot.revision == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ParticleRestoreError::RevisionExhausted);
        }
        // Every live particle required at least one successful insertion.
        // Replacements and impulses may increase the revision further, but no
        // canonical mutation can produce fewer revisions than live records.
        if (snapshot.particles.size() > snapshot.revision) {
            return std::unexpected(ParticleRestoreError::InvalidRevision);
        }
        std::map<ParticleId, ParticleState2> restored;
        for (auto& particle : snapshot.particles) {
            if (!particle.id || !particle.material) {
                return std::unexpected(ParticleRestoreError::InvalidIdentifier);
            }
            if (!std::isfinite(units::in_metres(particle.position[0])) ||
                !std::isfinite(units::in_metres(particle.position[1])) ||
                !std::isfinite(units::in_kilogram_metres_per_second(particle.momentum[0])) ||
                !std::isfinite(units::in_kilogram_metres_per_second(particle.momentum[1])) ||
                !std::isfinite(units::in_kilograms(particle.rest_mass))) {
                return std::unexpected(ParticleRestoreError::NonFiniteState);
            }
            if (units::in_kilograms(particle.rest_mass) <= 0.0) {
                return std::unexpected(ParticleRestoreError::NonPositiveMass);
            }
            switch (particle.constraint) {
            case KinematicConstraint::Free:
            case KinematicConstraint::Fixed:
                break;
            default:
                return std::unexpected(ParticleRestoreError::InvalidConstraint);
            }
            if (particle.constraint == KinematicConstraint::Fixed && !zero_momentum(particle.momentum)) {
                return std::unexpected(ParticleRestoreError::FixedParticleHasMomentum);
            }
            const auto id = particle.id;
            if (!restored.try_emplace(id, std::move(particle)).second) {
                return std::unexpected(ParticleRestoreError::DuplicateIdentifier);
            }
        }
        particles_ = std::move(restored);
        revision_ = snapshot.revision;
        return {};
    }

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const std::map<ParticleId, ParticleState2>& ordered_particles() const noexcept { return particles_; }

private:
    [[nodiscard]] static bool finite_position(const spacetime::WorldPosition<2>& position) noexcept
    {
        return std::isfinite(units::in_metres(position[0])) && std::isfinite(units::in_metres(position[1]));
    }

    [[nodiscard]] static bool finite_momentum(const math::Vector<2, units::Momentum>& momentum) noexcept
    {
        return std::isfinite(units::in_kilogram_metres_per_second(momentum[0])) &&
               std::isfinite(units::in_kilogram_metres_per_second(momentum[1]));
    }

    [[nodiscard]] static bool zero_momentum(const math::Vector<2, units::Momentum>& momentum) noexcept
    {
        return units::in_kilogram_metres_per_second(momentum[0]) == 0.0 &&
               units::in_kilogram_metres_per_second(momentum[1]) == 0.0;
    }

    [[nodiscard]] static std::expected<void, ParticleMutationError> validate_particle(
        const ParticleState2& particle) noexcept
    {
        if (!particle.id || !particle.material) {
            return std::unexpected(ParticleMutationError::InvalidIdentifier);
        }
        if (!finite_position(particle.position) || !finite_momentum(particle.momentum) ||
            !std::isfinite(units::in_kilograms(particle.rest_mass))) {
            return std::unexpected(ParticleMutationError::NonFiniteState);
        }
        if (units::in_kilograms(particle.rest_mass) <= 0.0) {
            return std::unexpected(ParticleMutationError::NonPositiveMass);
        }
        switch (particle.constraint) {
        case KinematicConstraint::Free:
            break;
        case KinematicConstraint::Fixed:
            if (!zero_momentum(particle.momentum)) {
                return std::unexpected(ParticleMutationError::FixedParticleHasMomentum);
            }
            break;
        default:
            return std::unexpected(ParticleMutationError::InvalidConstraint);
        }
        return {};
    }

    std::map<ParticleId, ParticleState2> particles_;
    std::uint64_t revision_{};
};

[[nodiscard]] inline math::Vector<2, units::Velocity> velocity_of(const ParticleState2& particle)
{
    return particle.momentum / particle.rest_mass;
}

}  // namespace principia::world
