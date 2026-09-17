#pragma once

#include <principia/diagnostics/inspection.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/game/reality_test_001.hpp>
#include <principia/operators/gravity_operator.hpp>

#include "cpu_canvas.hpp"

#include <map>

namespace principia::presentation {

void draw_scene(
    CpuCanvas& canvas,
    const diagnostics::WorldInspectionFrame2& inspection,
    const fields::EffectiveGravityField<2>& gravity,
    const std::map<world::ParticleId, game::EntityRole>& roles,
    const operators::GravityOperatorNode& operator_node,
    bool operator_installed,
    bool field_visible,
    bool paused);

}  // namespace principia::presentation
