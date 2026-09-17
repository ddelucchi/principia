#pragma once

#include <principia/fields/field.hpp>
#include <principia/math/vector.hpp>
#include <principia/units/quantity.hpp>

#include <limits>

namespace principia::fields {

template <std::size_t WorldDimension>
using GravityVector = math::Vector<WorldDimension, units::Acceleration>;

template <std::size_t WorldDimension>
using EffectiveGravityField = Field<GravityVector<WorldDimension>, WorldDimension>;

inline constexpr FieldTypeId effective_newtonian_gravity_field_id{1};

template <std::size_t WorldDimension>
[[nodiscard]] FieldMetadata effective_gravity_metadata()
{
    static_assert(WorldDimension > 0, "gravity fields require at least one physical component");
    static_assert(
        WorldDimension <= std::numeric_limits<std::uint8_t>::max(),
        "gravity field component counts must fit the persistent metadata representation");
    return FieldMetadata{
        effective_newtonian_gravity_field_id,
        "EffectiveNewtonianGravityField",
        state::FieldLocation::CellCentered,
        FieldStorageKind::Constant,
        static_cast<std::uint8_t>(WorldDimension),
    };
}

template <std::size_t WorldDimension>
[[nodiscard]] bool valid_effective_gravity_metadata(const FieldMetadata& metadata) noexcept
{
    static_assert(WorldDimension > 0);
    static_assert(WorldDimension <= std::numeric_limits<std::uint8_t>::max());
    return valid_field_metadata(metadata) && metadata.id == effective_newtonian_gravity_field_id &&
           metadata.location == state::FieldLocation::CellCentered &&
           metadata.physical_components == static_cast<std::uint8_t>(WorldDimension);
}

}  // namespace principia::fields
