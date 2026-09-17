#pragma once

#include <principia/units/quantity.hpp>
#include <principia/world/particle.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace principia::world {

struct DiscCollider2 {
    ParticleId particle;
    units::Length radius;

    friend bool operator==(const DiscCollider2&, const DiscCollider2&) = default;
};

enum class ColliderRegistryError : std::uint8_t {
    InvalidParticleId,
    NonFiniteRadius,
    NonPositiveRadius,
    DuplicateParticle,
    MissingParticle,
    InvalidRevision,
    RevisionExhausted,
};

[[nodiscard]] inline std::expected<void, ColliderRegistryError> validate_disc_collider(
    const DiscCollider2& collider)
{
    if (!collider.particle) {
        return std::unexpected(ColliderRegistryError::InvalidParticleId);
    }
    const auto radius = units::in_metres(collider.radius);
    if (!std::isfinite(radius)) {
        return std::unexpected(ColliderRegistryError::NonFiniteRadius);
    }
    if (radius <= 0.0) {
        return std::unexpected(ColliderRegistryError::NonPositiveRadius);
    }
    return {};
}

class DiscColliderRegistry2 {
public:
    [[nodiscard]] std::expected<std::uint64_t, ColliderRegistryError> try_add(DiscCollider2 collider)
    {
        const auto validity = validate_disc_collider(collider);
        if (!validity) {
            return std::unexpected(validity.error());
        }
        if (colliders_.contains(collider.particle)) {
            return std::unexpected(ColliderRegistryError::DuplicateParticle);
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ColliderRegistryError::RevisionExhausted);
        }
        const auto particle = collider.particle;
        colliders_.emplace(particle, std::move(collider));
        ++revision_;
        return revision_;
    }

    [[nodiscard]] std::expected<std::uint64_t, ColliderRegistryError> try_remove(ParticleId particle)
    {
        if (!particle) {
            return std::unexpected(ColliderRegistryError::InvalidParticleId);
        }
        const auto found = colliders_.find(particle);
        if (found == colliders_.end()) {
            return std::unexpected(ColliderRegistryError::MissingParticle);
        }
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ColliderRegistryError::RevisionExhausted);
        }
        colliders_.erase(found);
        ++revision_;
        return revision_;
    }

    [[nodiscard]] std::expected<void, ColliderRegistryError> restore(
        std::uint64_t revision,
        std::vector<DiscCollider2> colliders)
    {
        // With add/remove as the only mutations, revision - live_count is
        // exactly twice the number of historical removals.
        if (revision == std::numeric_limits<std::uint64_t>::max() || revision < colliders.size() ||
            ((revision - colliders.size()) % 2U) != 0U) {
            return std::unexpected(ColliderRegistryError::InvalidRevision);
        }
        std::map<ParticleId, DiscCollider2> restored;
        for (auto& collider : colliders) {
            const auto validity = validate_disc_collider(collider);
            if (!validity) {
                return std::unexpected(validity.error());
            }
            const auto particle = collider.particle;
            if (!restored.emplace(particle, std::move(collider)).second) {
                return std::unexpected(ColliderRegistryError::DuplicateParticle);
            }
        }
        colliders_ = std::move(restored);
        revision_ = revision;
        return {};
    }

    [[nodiscard]] const DiscCollider2* find(ParticleId particle) const noexcept
    {
        const auto found = colliders_.find(particle);
        return found == colliders_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const std::map<ParticleId, DiscCollider2>& ordered_colliders() const noexcept
    {
        return colliders_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return colliders_.size(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::map<ParticleId, DiscCollider2> colliders_;
    std::uint64_t revision_{};
};

}  // namespace principia::world
