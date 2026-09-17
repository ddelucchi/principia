#pragma once

#include <principia/boundaries/boundary.hpp>
#include <principia/core/strong_id.hpp>
#include <principia/operators/gravity_operator.hpp>
#include <principia/spacetime/geometry.hpp>
#include <principia/units/quantity.hpp>

#include <cstdint>
#include <vector>

namespace principia::operators {

enum class PhysicalParameterKind : std::uint8_t {
    FundamentalConstant,
    EffectiveMaterialParameter,
    GeometricalEffect,
    FictionalLawOverride,
};

struct EffectiveMediumIdTag;
using EffectiveMediumId = core::StrongId<EffectiveMediumIdTag, std::uint32_t>;

struct FundamentalConstants {
    units::Velocity invariant_speed{units::metres_per_second(299'792'458.0)};
};

struct EffectiveMediumContext {
    EffectiveMediumId medium{};
};

struct LawContext {
    spacetime::GeometryContext geometry;
    FundamentalConstants constants;
    EffectiveMediumContext medium;
    std::vector<boundaries::BoundaryId> active_boundaries;
    std::vector<OperatorId> active_overlays;
};

}  // namespace principia::operators

