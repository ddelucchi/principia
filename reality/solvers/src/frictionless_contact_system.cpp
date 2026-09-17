#include <principia/solvers/frictionless_contact_system.hpp>

#include <principia/state/channel.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace principia::solvers {

namespace {

using SystemError = FrictionlessContactSystemError;

[[nodiscard]] math::Vector<2, units::Momentum> zero_momentum()
{
    return math::Vector<2, units::Momentum>{
        units::kilogram_metres_per_second(0.0),
        units::kilogram_metres_per_second(0.0),
    };
}

[[nodiscard]] bool finite_position(const spacetime::WorldPosition<2>& position)
{
    return std::isfinite(units::in_metres(position[0])) &&
           std::isfinite(units::in_metres(position[1]));
}

[[nodiscard]] bool finite_momentum(const math::Vector<2, units::Momentum>& momentum)
{
    return std::isfinite(units::in_kilogram_metres_per_second(momentum[0])) &&
           std::isfinite(units::in_kilogram_metres_per_second(momentum[1]));
}

[[nodiscard]] bool finite_body(const ContactBody2& body)
{
    return body.state.id && body.state.material && finite_position(body.state.position) &&
           finite_momentum(body.state.momentum) &&
           std::isfinite(units::in_kilograms(body.state.rest_mass)) &&
           std::isfinite(units::in_metres(body.radius));
}

[[nodiscard]] bool finite_configuration(const FrictionlessContactSystemConfiguration& configuration)
{
    return configuration.maximum_bodies > 0 && configuration.maximum_boundaries > 0 &&
           std::isfinite(configuration.geometric_tolerance_metres) &&
           configuration.geometric_tolerance_metres >= 0.0 &&
           std::isfinite(configuration.simultaneous_time_tolerance_seconds) &&
           configuration.simultaneous_time_tolerance_seconds >= 0.0;
}

[[nodiscard]] bool mechanically_rigid(const boundaries::BoundaryDefinition& boundary)
{
    const auto found = boundary.conditions.find(state::standard_channels::position_id);
    return found != boundary.conditions.end() &&
           found->second == boundaries::BoundaryConditionKind::MechanicallyRigid;
}

[[nodiscard]] units::AngularMomentum angular_impulse_about_origin(
    const spacetime::WorldPosition<2>& point,
    const math::Vector<2, units::Momentum>& impulse)
{
    return point[0] * impulse[1] - point[1] * impulse[0];
}

[[nodiscard]] spacetime::WorldPosition<2> drifted_position(
    const world::ParticleState2& state,
    double seconds)
{
    return state.position + world::velocity_of(state) * units::seconds(seconds);
}

[[nodiscard]] std::expected<void, SystemError> validate_registries(
    const boundaries::BoundaryRegistry& boundary_registry,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& responses)
{
    for (const auto& [id, boundary] : boundary_registry.ordered_definitions()) {
        if (id != boundary.id || !boundaries::validate_boundary_definition(boundary)) {
            return std::unexpected(SystemError::InvalidBoundaryRegistry);
        }
    }
    for (const auto& [id, response] : responses.ordered_definitions()) {
        if (id != response.id || !materials::validate_mechanical_response(response)) {
            return std::unexpected(SystemError::InvalidMechanicalResponseRegistry);
        }
    }
    for (const auto& [id, material] : materials.ordered_definitions()) {
        if (id != material.id ||
            !materials::validate_material_definition(material, materials.model_catalog())) {
            return std::unexpected(SystemError::InvalidMaterialRegistry);
        }
    }
    return {};
}

[[nodiscard]] std::expected<const materials::MechanicalResponseDefinition*, SystemError> response_for(
    const ContactBody2& body,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& responses)
{
    const auto* material = materials.find(body.state.material);
    if (material == nullptr) {
        return std::unexpected(SystemError::MissingMaterial);
    }
    const auto* response = responses.find(material->mechanical);
    if (response == nullptr) {
        return std::unexpected(SystemError::MissingMechanicalResponse);
    }
    if (response->impact_kind != materials::ImpactResponseKind::FrictionlessRestitution) {
        return std::unexpected(SystemError::UnsupportedMechanicalResponse);
    }
    return response;
}

struct ContactCandidate {
    double time_seconds{};
    FrictionlessContactSystemEventKind kind{FrictionlessContactSystemEventKind::StaticBoundary};
    std::size_t first_index{};
    std::optional<std::size_t> second_index;
    std::optional<boundaries::BoundaryId> boundary;
    math::Vector<2, double> normal_from_second_to_first;
};

[[nodiscard]] std::uint64_t second_persistent_id(
    const ContactCandidate& candidate,
    const std::vector<ContactBody2>& bodies)
{
    if (candidate.second_index) {
        return bodies[*candidate.second_index].state.id.value();
    }
    return candidate.boundary ? candidate.boundary->value() : 0U;
}

[[nodiscard]] bool candidate_less(
    const ContactCandidate& left,
    const ContactCandidate& right,
    const std::vector<ContactBody2>& bodies)
{
    if (left.time_seconds != right.time_seconds) {
        return left.time_seconds < right.time_seconds;
    }
    if (left.kind != right.kind) {
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    }
    const auto left_first = bodies[left.first_index].state.id;
    const auto right_first = bodies[right.first_index].state.id;
    if (left_first != right_first) {
        return left_first < right_first;
    }
    return second_persistent_id(left, bodies) < second_persistent_id(right, bodies);
}

[[nodiscard]] bool candidates_share_particle(
    const ContactCandidate& left,
    const ContactCandidate& right)
{
    if (left.first_index == right.first_index) {
        return true;
    }
    if (left.second_index &&
        (*left.second_index == right.first_index ||
         (right.second_index && *left.second_index == *right.second_index))) {
        return true;
    }
    return right.second_index && *right.second_index == left.first_index;
}

struct BoundaryFeatureCandidate {
    double time_seconds{};
    math::Vector<2, double> normal;
};

struct CircleSweepRoot {
    double time_seconds{};
    std::array<double, 2> relative_position_at_contact{};
};

[[nodiscard]] bool same_normal(
    const math::Vector<2, double>& left,
    const math::Vector<2, double>& right)
{
    constexpr auto normal_tolerance = 64.0 * std::numeric_limits<double>::epsilon();
    return std::abs(left[0] - right[0]) <= normal_tolerance &&
           std::abs(left[1] - right[1]) <= normal_tolerance;
}

// Solves |relative_position + relative_velocity * t| = radius in a
// velocity-aligned frame. This avoids subtracting the O(distance^2) terms in
// the quadratic discriminant, which loses the impact parameter for long
// baselines and near-grazing paths.
[[nodiscard]] std::expected<std::optional<CircleSweepRoot>, SystemError>
sweep_point_against_circle(
    const std::array<double, 2>& relative_position,
    const std::array<double, 2>& relative_velocity,
    double radius,
    double remaining_seconds,
    double geometric_tolerance_metres)
{
    const auto speed = std::hypot(relative_velocity[0], relative_velocity[1]);
    const auto distance = std::hypot(relative_position[0], relative_position[1]);
    if (!std::isfinite(speed) || !std::isfinite(distance) || !std::isfinite(radius) ||
        !std::isfinite(remaining_seconds) || radius <= 0.0 || remaining_seconds < 0.0) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (speed == 0.0) {
        return std::optional<CircleSweepRoot>{};
    }

    const std::array<double, 2> direction{
        relative_velocity[0] / speed,
        relative_velocity[1] / speed,
    };
    // `normal_to_path` is a canonical counterclockwise quarter turn.
    const std::array<double, 2> normal_to_path{-direction[1], direction[0]};
    const auto along = relative_position[0] * direction[0] +
                       relative_position[1] * direction[1];
    const auto lateral = relative_position[0] * normal_to_path[0] +
                         relative_position[1] * normal_to_path[1];
    if (!std::isfinite(along) || !std::isfinite(lateral)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (along >= 0.0) {
        return std::optional<CircleSweepRoot>{};
    }

    const auto lateral_distance = std::abs(lateral);
    constexpr auto roundoff_multiplier = 128.0;
    const auto projection_uncertainty =
        roundoff_multiplier * std::numeric_limits<double>::epsilon() *
        std::max({distance, radius, 1.0});
    if (!std::isfinite(projection_uncertainty)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (lateral_distance == radius) {
        // Exact tangency has zero closing-normal speed and therefore no
        // frictionless impulse.
        return std::optional<CircleSweepRoot>{};
    }
    if (std::abs(lateral_distance - radius) <= projection_uncertainty) {
        // At this scale the stored doubles cannot distinguish a grazing hit
        // from a grazing miss. A deterministic explicit failure is safer than
        // selecting either topology from roundoff noise.
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (lateral_distance > radius) {
        return std::optional<CircleSweepRoot>{};
    }

    const auto chord_squared = (radius - lateral_distance) * (radius + lateral_distance);
    if (!std::isfinite(chord_squared) || chord_squared < 0.0) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    const auto chord = std::sqrt(chord_squared);
    const auto forward_distance = -along;
    auto path_distance = forward_distance - chord;
    if (!std::isfinite(chord) || !std::isfinite(path_distance)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (path_distance < 0.0) {
        const auto path_uncertainty =
            geometric_tolerance_metres +
            roundoff_multiplier * std::numeric_limits<double>::epsilon() *
                std::max({forward_distance, chord, 1.0});
        if (!std::isfinite(path_uncertainty) || -path_distance > path_uncertainty) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        path_distance = 0.0;
    }
    const auto time_seconds = path_distance / speed;
    if (!std::isfinite(time_seconds)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    // The simulation horizon is exact. Simultaneity tolerance may group two
    // admitted events, but it must never pull a future event into this step.
    if (time_seconds < 0.0 || time_seconds > remaining_seconds) {
        return std::optional<CircleSweepRoot>{};
    }

    const std::array<double, 2> contact_relative{
        -chord * direction[0] + lateral * normal_to_path[0],
        -chord * direction[1] + lateral * normal_to_path[1],
    };
    const auto reconstructed_distance =
        std::hypot(contact_relative[0], contact_relative[1]);
    const std::array<double, 2> travel{
        relative_velocity[0] * time_seconds,
        relative_velocity[1] * time_seconds,
    };
    const std::array<double, 2> directly_evaluated_contact{
        relative_position[0] + travel[0],
        relative_position[1] + travel[1],
    };
    const auto directly_evaluated_distance =
        std::hypot(directly_evaluated_contact[0], directly_evaluated_contact[1]);
    const auto arithmetic_scale = std::max({
        std::abs(relative_position[0]),
        std::abs(relative_position[1]),
        std::abs(travel[0]),
        std::abs(travel[1]),
        radius,
        1.0,
    });
    const auto manifold_tolerance =
        geometric_tolerance_metres +
        roundoff_multiplier * std::numeric_limits<double>::epsilon() * arithmetic_scale;
    if (!std::isfinite(reconstructed_distance) ||
        !std::isfinite(directly_evaluated_distance) || !std::isfinite(manifold_tolerance) ||
        std::abs(reconstructed_distance - radius) > manifold_tolerance ||
        std::abs(directly_evaluated_distance - radius) > manifold_tolerance) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    return std::optional<CircleSweepRoot>{CircleSweepRoot{
        time_seconds,
        contact_relative,
    }};
}

[[nodiscard]] std::expected<std::optional<ContactCandidate>, SystemError> boundary_candidate(
    const ContactBody2& body,
    std::size_t body_index,
    const boundaries::BoundaryDefinition& boundary,
    double remaining_seconds,
    const FrictionlessContactSystemConfiguration& configuration,
    bool allow_separating_roundoff_overlap)
{
    const std::array<double, 2> start{
        units::in_metres(body.state.position[0]),
        units::in_metres(body.state.position[1]),
    };
    const auto velocity_quantity = world::velocity_of(body.state);
    const std::array<double, 2> velocity{
        units::in_metres_per_second(velocity_quantity[0]),
        units::in_metres_per_second(velocity_quantity[1]),
    };
    const std::array<double, 2> minimum{
        units::in_metres(boundary.extent.minimum[0]),
        units::in_metres(boundary.extent.minimum[1]),
    };
    const std::array<double, 2> maximum{
        units::in_metres(boundary.extent.maximum[0]),
        units::in_metres(boundary.extent.maximum[1]),
    };
    const auto radius = units::in_metres(body.radius);
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(start[axis]) || !std::isfinite(velocity[axis]) ||
            !std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis])) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
    }

    const std::array<double, 2> closest{
        std::clamp(start[0], minimum[0], maximum[0]),
        std::clamp(start[1], minimum[1], maximum[1]),
    };
    const auto initial_dx = start[0] - closest[0];
    const auto initial_dy = start[1] - closest[1];
    const auto initial_distance = std::hypot(initial_dx, initial_dy);
    if (!std::isfinite(initial_distance)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (initial_distance < radius) {
        const auto absolute_position_scale = std::max({
            std::abs(start[0]),
            std::abs(start[1]),
            std::abs(minimum[0]),
            std::abs(minimum[1]),
            std::abs(maximum[0]),
            std::abs(maximum[1]),
            radius,
            1.0,
        });
        const auto roundoff_tolerance =
            configuration.geometric_tolerance_metres +
            128.0 * std::numeric_limits<double>::epsilon() * absolute_position_scale;
        const auto outward_speed = initial_distance > 0.0
                                       ? (velocity[0] * initial_dx + velocity[1] * initial_dy) /
                                             initial_distance
                                       : 0.0;
        if (allow_separating_roundoff_overlap && outward_speed > 0.0 &&
            std::isfinite(roundoff_tolerance) && radius - initial_distance <= roundoff_tolerance) {
            return std::optional<ContactCandidate>{};
        }
        return std::unexpected(SystemError::InitialOverlap);
    }

    std::optional<BoundaryFeatureCandidate> selected;
    const auto consider = [&](double time_seconds, math::Vector<2, double> normal)
        -> std::expected<void, SystemError> {
        if (!std::isfinite(time_seconds) || !std::isfinite(normal[0]) || !std::isfinite(normal[1])) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        if (time_seconds < 0.0 || time_seconds > remaining_seconds) {
            return {};
        }
        const auto inward_speed = velocity[0] * normal[0] + velocity[1] * normal[1];
        if (!std::isfinite(inward_speed)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        if (inward_speed >= 0.0) {
            return {};
        }
        if (!selected || time_seconds < selected->time_seconds) {
            selected = BoundaryFeatureCandidate{time_seconds, normal};
            return {};
        }
        if (std::abs(time_seconds - selected->time_seconds) <=
                configuration.simultaneous_time_tolerance_seconds &&
            !same_normal(normal, selected->normal)) {
            return std::unexpected(SystemError::UnsupportedCoupledSimultaneousContact);
        }
        return {};
    };

    if (initial_distance == radius && initial_distance > 0.0) {
        const auto initial_contact = consider(
            0.0,
            math::Vector<2, double>{
                initial_dx / initial_distance,
                initial_dy / initial_distance,
            });
        if (!initial_contact) {
            return std::unexpected(initial_contact.error());
        }
    }

    const auto geometry_tolerance = configuration.geometric_tolerance_metres;
    if (velocity[0] > 0.0) {
        const auto time = (minimum[0] - radius - start[0]) / velocity[0];
        const auto y = start[1] + velocity[1] * time;
        if (std::isfinite(y) && y >= minimum[1] - geometry_tolerance &&
            y <= maximum[1] + geometry_tolerance) {
            const auto feature = consider(time, math::Vector<2, double>{-1.0, 0.0});
            if (!feature) {
                return std::unexpected(feature.error());
            }
        }
    } else if (velocity[0] < 0.0) {
        const auto time = (maximum[0] + radius - start[0]) / velocity[0];
        const auto y = start[1] + velocity[1] * time;
        if (std::isfinite(y) && y >= minimum[1] - geometry_tolerance &&
            y <= maximum[1] + geometry_tolerance) {
            const auto feature = consider(time, math::Vector<2, double>{1.0, 0.0});
            if (!feature) {
                return std::unexpected(feature.error());
            }
        }
    }
    if (velocity[1] > 0.0) {
        const auto time = (minimum[1] - radius - start[1]) / velocity[1];
        const auto x = start[0] + velocity[0] * time;
        if (std::isfinite(x) && x >= minimum[0] - geometry_tolerance &&
            x <= maximum[0] + geometry_tolerance) {
            const auto feature = consider(time, math::Vector<2, double>{0.0, -1.0});
            if (!feature) {
                return std::unexpected(feature.error());
            }
        }
    } else if (velocity[1] < 0.0) {
        const auto time = (maximum[1] + radius - start[1]) / velocity[1];
        const auto x = start[0] + velocity[0] * time;
        if (std::isfinite(x) && x >= minimum[0] - geometry_tolerance &&
            x <= maximum[0] + geometry_tolerance) {
            const auto feature = consider(time, math::Vector<2, double>{0.0, 1.0});
            if (!feature) {
                return std::unexpected(feature.error());
            }
        }
    }

    const auto speed = std::hypot(velocity[0], velocity[1]);
    if (!std::isfinite(speed)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (speed > 0.0) {
        struct Corner {
            double x{};
            double y{};
            bool left{};
            bool lower{};
        };
        const std::array<Corner, 4> corners{
            Corner{minimum[0], minimum[1], true, true},
            Corner{minimum[0], maximum[1], true, false},
            Corner{maximum[0], minimum[1], false, true},
            Corner{maximum[0], maximum[1], false, false},
        };
        for (const auto& corner : corners) {
            const auto dx = start[0] - corner.x;
            const auto dy = start[1] - corner.y;
            const auto root = sweep_point_against_circle(
                std::array<double, 2>{dx, dy},
                velocity,
                radius,
                remaining_seconds,
                geometry_tolerance);
            if (!root) {
                return std::unexpected(root.error());
            }
            if (!*root) {
                continue;
            }
            const auto time = (*root)->time_seconds;
            const auto x = corner.x + (*root)->relative_position_at_contact[0];
            const auto y = corner.y + (*root)->relative_position_at_contact[1];
            const auto in_x_region = corner.left ? x <= corner.x + geometry_tolerance
                                                 : x >= corner.x - geometry_tolerance;
            const auto in_y_region = corner.lower ? y <= corner.y + geometry_tolerance
                                                  : y >= corner.y - geometry_tolerance;
            if (!in_x_region || !in_y_region) {
                continue;
            }
            const auto normal_distance = std::hypot(
                (*root)->relative_position_at_contact[0],
                (*root)->relative_position_at_contact[1]);
            if (!std::isfinite(normal_distance) || normal_distance <= 0.0) {
                return std::unexpected(SystemError::NumericalOverflow);
            }
            const auto feature = consider(
                time,
                math::Vector<2, double>{
                    (*root)->relative_position_at_contact[0] / normal_distance,
                    (*root)->relative_position_at_contact[1] / normal_distance,
                });
            if (!feature) {
                return std::unexpected(feature.error());
            }
        }
    }

    if (!selected) {
        return std::optional<ContactCandidate>{};
    }
    return std::optional<ContactCandidate>{ContactCandidate{
        selected->time_seconds,
        FrictionlessContactSystemEventKind::StaticBoundary,
        body_index,
        std::nullopt,
        boundary.id,
        selected->normal,
    }};
}

[[nodiscard]] std::expected<std::optional<ContactCandidate>, SystemError> pair_candidate(
    const ContactBody2& first,
    std::size_t first_index,
    const ContactBody2& second,
    std::size_t second_index,
    double remaining_seconds,
    const FrictionlessContactSystemConfiguration& configuration,
    bool allow_separating_roundoff_overlap)
{
    const std::array<double, 2> separation{
        units::in_metres(second.state.position[0] - first.state.position[0]),
        units::in_metres(second.state.position[1] - first.state.position[1]),
    };
    const auto first_velocity_quantity = world::velocity_of(first.state);
    const auto second_velocity_quantity = world::velocity_of(second.state);
    const std::array<double, 2> relative_velocity{
        units::in_metres_per_second(second_velocity_quantity[0] - first_velocity_quantity[0]),
        units::in_metres_per_second(second_velocity_quantity[1] - first_velocity_quantity[1]),
    };
    const auto radius = units::in_metres(first.radius) + units::in_metres(second.radius);
    const auto distance = std::hypot(separation[0], separation[1]);
    if (!std::isfinite(separation[0]) || !std::isfinite(separation[1]) ||
        !std::isfinite(relative_velocity[0]) || !std::isfinite(relative_velocity[1]) ||
        !std::isfinite(radius) || !std::isfinite(distance)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (distance < radius) {
        const auto separation_rate = separation[0] * relative_velocity[0] +
                                     separation[1] * relative_velocity[1];
        if (!std::isfinite(separation_rate)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        const auto absolute_position_scale = std::max({
            std::abs(units::in_metres(first.state.position[0])),
            std::abs(units::in_metres(first.state.position[1])),
            std::abs(units::in_metres(second.state.position[0])),
            std::abs(units::in_metres(second.state.position[1])),
            radius,
            1.0,
        });
        const auto roundoff_tolerance =
            configuration.geometric_tolerance_metres +
            128.0 * std::numeric_limits<double>::epsilon() * absolute_position_scale;
        if (allow_separating_roundoff_overlap && separation_rate > 0.0 &&
            std::isfinite(roundoff_tolerance) && radius - distance <= roundoff_tolerance) {
            return std::optional<ContactCandidate>{};
        }
        return std::unexpected(SystemError::InitialOverlap);
    }

    const auto root = sweep_point_against_circle(
        separation,
        relative_velocity,
        radius,
        remaining_seconds,
        configuration.geometric_tolerance_metres);
    if (!root) {
        return std::unexpected(root.error());
    }
    if (!*root) {
        return std::optional<ContactCandidate>{};
    }
    const auto time = (*root)->time_seconds;
    const auto contact_dx = (*root)->relative_position_at_contact[0];
    const auto contact_dy = (*root)->relative_position_at_contact[1];
    const auto contact_distance = std::hypot(contact_dx, contact_dy);
    if (!std::isfinite(contact_distance) || contact_distance <= 0.0) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    const math::Vector<2, double> normal_from_second_to_first{
        -contact_dx / contact_distance,
        -contact_dy / contact_distance,
    };
    const auto first_relative_normal_speed =
        -relative_velocity[0] * normal_from_second_to_first[0] -
        relative_velocity[1] * normal_from_second_to_first[1];
    if (!std::isfinite(first_relative_normal_speed)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    if (first_relative_normal_speed >= 0.0) {
        return std::optional<ContactCandidate>{};
    }
    return std::optional<ContactCandidate>{ContactCandidate{
        time,
        FrictionlessContactSystemEventKind::ParticlePair,
        first_index,
        second_index,
        std::nullopt,
        normal_from_second_to_first,
    }};
}

[[nodiscard]] std::expected<void, SystemError> drift_all(
    std::vector<ContactBody2>& bodies,
    double seconds)
{
    std::vector<spacetime::WorldPosition<2>> positions;
    positions.reserve(bodies.size());
    for (const auto& body : bodies) {
        const auto position = drifted_position(body.state, seconds);
        if (!finite_position(position)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        positions.push_back(position);
    }
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        bodies[index].state.position = positions[index];
    }
    return {};
}

[[nodiscard]] std::expected<bool, SystemError> selected_event_has_coupled_geometric_contact(
    const ContactCandidate& selected,
    const std::vector<ContactBody2>& bodies,
    const boundaries::BoundaryRegistry& boundary_registry,
    double time_seconds,
    double geometric_tolerance_metres)
{
    std::vector<spacetime::WorldPosition<2>> positions;
    positions.reserve(bodies.size());
    for (const auto& body : bodies) {
        const auto position = drifted_position(body.state, time_seconds);
        if (!finite_position(position)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        positions.push_back(position);
    }

    const auto selected_contains = [&](std::size_t index) {
        return index == selected.first_index ||
               (selected.second_index && index == *selected.second_index);
    };
    for (std::size_t first = 0; first < bodies.size(); ++first) {
        for (std::size_t second = first + 1; second < bodies.size(); ++second) {
            if (!selected_contains(first) && !selected_contains(second)) {
                continue;
            }
            const auto is_selected_pair =
                selected.kind == FrictionlessContactSystemEventKind::ParticlePair &&
                first == selected.first_index && selected.second_index &&
                second == *selected.second_index;
            if (is_selected_pair) {
                continue;
            }
            const auto dx = units::in_metres(positions[second][0] - positions[first][0]);
            const auto dy = units::in_metres(positions[second][1] - positions[first][1]);
            const auto distance = std::hypot(dx, dy);
            const auto radius = units::in_metres(bodies[first].radius) +
                                units::in_metres(bodies[second].radius);
            if (!std::isfinite(distance) || !std::isfinite(radius)) {
                return std::unexpected(SystemError::NumericalOverflow);
            }
            if (distance <= radius + geometric_tolerance_metres) {
                return true;
            }
        }
    }

    for (std::size_t body_index = 0; body_index < bodies.size(); ++body_index) {
        if (!selected_contains(body_index)) {
            continue;
        }
        const auto x = units::in_metres(positions[body_index][0]);
        const auto y = units::in_metres(positions[body_index][1]);
        const auto radius = units::in_metres(bodies[body_index].radius);
        for (const auto& [boundary_id, boundary] : boundary_registry.ordered_definitions()) {
            if (!mechanically_rigid(boundary)) {
                continue;
            }
            const auto is_selected_boundary =
                selected.kind == FrictionlessContactSystemEventKind::StaticBoundary &&
                body_index == selected.first_index && selected.boundary &&
                boundary_id == *selected.boundary;
            if (is_selected_boundary) {
                continue;
            }
            const auto minimum_x = units::in_metres(boundary.extent.minimum[0]);
            const auto minimum_y = units::in_metres(boundary.extent.minimum[1]);
            const auto maximum_x = units::in_metres(boundary.extent.maximum[0]);
            const auto maximum_y = units::in_metres(boundary.extent.maximum[1]);
            const auto closest_x = std::clamp(x, minimum_x, maximum_x);
            const auto closest_y = std::clamp(y, minimum_y, maximum_y);
            const auto distance = std::hypot(x - closest_x, y - closest_y);
            if (!std::isfinite(distance)) {
                return std::unexpected(SystemError::NumericalOverflow);
            }
            if (distance <= radius + geometric_tolerance_metres) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::expected<units::Energy, SystemError> analytic_restitution_energy_loss(
    double impulse_magnitude_kilogram_metres_per_second,
    double closing_normal_speed_metres_per_second,
    double restitution)
{
    if (!std::isfinite(impulse_magnitude_kilogram_metres_per_second) ||
        !std::isfinite(closing_normal_speed_metres_per_second) ||
        !std::isfinite(restitution) ||
        impulse_magnitude_kilogram_metres_per_second < 0.0 ||
        closing_normal_speed_metres_per_second < 0.0 || restitution < 0.0 ||
        restitution > 1.0) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    // J = (1 + e) m_eff v_n, so the exactly dissipated normal energy is
    // 1/2 (1 - e) J v_n. Multiplying by the small (1-e) factor first keeps a
    // representable loss finite even when J*v_n alone would overflow.
    const auto scaled_impulse =
        impulse_magnitude_kilogram_metres_per_second * ((1.0 - restitution) * 0.5);
    const auto loss_joules = scaled_impulse * closing_normal_speed_metres_per_second;
    if (!std::isfinite(scaled_impulse) || !std::isfinite(loss_joules) || loss_joules < 0.0) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    return units::joules(loss_joules);
}

[[nodiscard]] spacetime::WorldPosition<2> contact_point_on_first(
    const ContactBody2& body,
    const math::Vector<2, double>& normal_from_second_to_first)
{
    return body.state.position - math::Vector<2, units::Length>{
                                     body.radius * normal_from_second_to_first[0],
                                     body.radius * normal_from_second_to_first[1],
                                 };
}

[[nodiscard]] bool finite_result_state(const FrictionlessContactSystemDriftResult2& result)
{
    if (result.bodies.size() != result.particle_impulses.size() ||
        !finite_momentum(result.boundary_impulse) ||
        !std::isfinite(units::in_kilogram_square_metres_per_second(
            result.boundary_angular_impulse)) ||
        !std::isfinite(units::in_joules(result.unresolved_internal_energy_flux))) {
        return false;
    }
    for (std::size_t index = 0; index < result.bodies.size(); ++index) {
        if (!finite_body(result.bodies[index]) ||
            result.particle_impulses[index].particle != result.bodies[index].state.id ||
            !finite_momentum(result.particle_impulses[index].boundary_impulse) ||
            !finite_momentum(result.particle_impulses[index].internal_impulse)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::expected<FrictionlessContactSystemDriftResult2, FrictionlessContactSystemError>
resolve_frictionless_contact_system_drift(
    std::vector<ContactBody2> bodies,
    const boundaries::BoundaryRegistry& boundary_registry,
    const materials::MaterialRegistry& materials,
    const materials::MechanicalResponseRegistry& mechanical_responses,
    units::Duration dt,
    FrictionlessContactSystemConfiguration configuration)
{
    const auto dt_seconds = units::in_seconds(dt);
    if (!std::isfinite(dt_seconds)) {
        return std::unexpected(SystemError::NonFiniteInput);
    }
    if (dt_seconds <= 0.0) {
        return std::unexpected(SystemError::NonPositiveTimeStep);
    }
    if (!finite_configuration(configuration)) {
        return std::unexpected(SystemError::InvalidConfiguration);
    }
    if (bodies.size() > configuration.maximum_bodies) {
        return std::unexpected(SystemError::BodyLimitExceeded);
    }
    if (boundary_registry.size() > configuration.maximum_boundaries) {
        return std::unexpected(SystemError::BoundaryLimitExceeded);
    }
    if (const auto registry_validity =
            validate_registries(boundary_registry, materials, mechanical_responses);
        !registry_validity) {
        return std::unexpected(registry_validity.error());
    }

    std::ranges::sort(bodies, {}, [](const ContactBody2& body) { return body.state.id; });
    std::vector<const materials::MechanicalResponseDefinition*> responses;
    responses.reserve(bodies.size());
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        const auto& body = bodies[index];
        if (!body.state.id || !body.state.material) {
            return std::unexpected(SystemError::InvalidIdentifier);
        }
        if (index > 0 && body.state.id == bodies[index - 1].state.id) {
            return std::unexpected(SystemError::DuplicateParticle);
        }
        if (!finite_body(body)) {
            return std::unexpected(SystemError::NonFiniteInput);
        }
        if (units::in_kilograms(body.state.rest_mass) <= 0.0) {
            return std::unexpected(SystemError::NonPositiveMass);
        }
        if (units::in_metres(body.radius) <= 0.0) {
            return std::unexpected(SystemError::NonPositiveRadius);
        }
        if (body.state.constraint != world::KinematicConstraint::Free) {
            return std::unexpected(SystemError::InvalidConstraint);
        }
        const auto response = response_for(body, materials, mechanical_responses);
        if (!response) {
            return std::unexpected(response.error());
        }
        responses.push_back(*response);
    }

    FrictionlessContactSystemDriftResult2 result;
    result.bodies = std::move(bodies);
    result.boundary_impulse = zero_momentum();
    result.particle_impulses.reserve(result.bodies.size());
    for (const auto& body : result.bodies) {
        result.particle_impulses.push_back(FrictionlessContactParticleImpulse2{
            body.state.id,
            zero_momentum(),
            zero_momentum(),
        });
    }

    auto elapsed_seconds = 0.0;
    auto remaining_seconds = dt_seconds;
    while (remaining_seconds > 0.0) {
        std::vector<ContactCandidate> candidates;
        for (std::size_t body_index = 0; body_index < result.bodies.size(); ++body_index) {
            for (const auto& [boundary_id, boundary] : boundary_registry.ordered_definitions()) {
                static_cast<void>(boundary_id);
                if (!mechanically_rigid(boundary)) {
                    continue;
                }
                const auto candidate = boundary_candidate(
                    result.bodies[body_index],
                    body_index,
                    boundary,
                    remaining_seconds,
                    configuration,
                    !result.contacts.empty());
                if (!candidate) {
                    return std::unexpected(candidate.error());
                }
                if (*candidate) {
                    candidates.push_back(**candidate);
                }
            }
        }
        for (std::size_t first_index = 0; first_index < result.bodies.size(); ++first_index) {
            for (std::size_t second_index = first_index + 1;
                 second_index < result.bodies.size();
                 ++second_index) {
                const auto candidate = pair_candidate(
                    result.bodies[first_index],
                    first_index,
                    result.bodies[second_index],
                    second_index,
                    remaining_seconds,
                    configuration,
                    !result.contacts.empty());
                if (!candidate) {
                    return std::unexpected(candidate.error());
                }
                if (*candidate) {
                    candidates.push_back(**candidate);
                }
            }
        }

        if (candidates.empty()) {
            if (const auto drift = drift_all(result.bodies, remaining_seconds); !drift) {
                return std::unexpected(drift.error());
            }
            elapsed_seconds += remaining_seconds;
            remaining_seconds = 0.0;
            break;
        }

        const auto selected_position = std::ranges::min_element(
            candidates,
            [&](const ContactCandidate& left, const ContactCandidate& right) {
                return candidate_less(left, right, result.bodies);
            });
        const auto selected = *selected_position;
        const auto simultaneous_tolerance = configuration.simultaneous_time_tolerance_seconds;
        std::vector<const ContactCandidate*> simultaneous;
        simultaneous.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            if (std::abs(candidate.time_seconds - selected.time_seconds) <= simultaneous_tolerance) {
                simultaneous.push_back(&candidate);
            }
        }
        for (std::size_t left = 0; left < simultaneous.size(); ++left) {
            for (std::size_t right = left + 1; right < simultaneous.size(); ++right) {
                if (candidates_share_particle(*simultaneous[left], *simultaneous[right])) {
                    return std::unexpected(SystemError::UnsupportedCoupledSimultaneousContact);
                }
            }
        }
        const auto geometrically_coupled = selected_event_has_coupled_geometric_contact(
            selected,
            result.bodies,
            boundary_registry,
            selected.time_seconds,
            configuration.geometric_tolerance_metres);
        if (!geometrically_coupled) {
            return std::unexpected(geometrically_coupled.error());
        }
        if (*geometrically_coupled) {
            return std::unexpected(SystemError::UnsupportedCoupledSimultaneousContact);
        }
        if (result.contacts.size() >= configuration.maximum_events) {
            return std::unexpected(SystemError::EventLimitExceeded);
        }
        if (const auto drift = drift_all(result.bodies, selected.time_seconds); !drift) {
            return std::unexpected(drift.error());
        }
        elapsed_seconds += selected.time_seconds;
        remaining_seconds -= selected.time_seconds;
        if (!std::isfinite(elapsed_seconds) || !std::isfinite(remaining_seconds)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        remaining_seconds = std::max(0.0, remaining_seconds);

        auto& first = result.bodies[selected.first_index];
        auto& first_ledger = result.particle_impulses[selected.first_index];
        const auto contact_point =
            contact_point_on_first(first, selected.normal_from_second_to_first);
        if (!finite_position(contact_point)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }

        math::Vector<2, units::Momentum> impulse_on_first = zero_momentum();
        auto energy_loss = units::joules(0.0);
        std::optional<world::ParticleId> second_particle;
        if (selected.kind == FrictionlessContactSystemEventKind::StaticBoundary) {
            const auto restitution =
                responses[selected.first_index]->normal_coefficient_of_restitution;
            const auto normal_momentum =
                units::in_kilogram_metres_per_second(first.state.momentum[0]) *
                    selected.normal_from_second_to_first[0] +
                units::in_kilogram_metres_per_second(first.state.momentum[1]) *
                    selected.normal_from_second_to_first[1];
            const auto impulse_magnitude = -(1.0 + restitution) * normal_momentum;
            const auto closing_normal_speed =
                -normal_momentum / units::in_kilograms(first.state.rest_mass);
            const auto loss = analytic_restitution_energy_loss(
                impulse_magnitude,
                closing_normal_speed,
                restitution);
            if (!loss) {
                return std::unexpected(loss.error());
            }
            energy_loss = *loss;
            impulse_on_first = math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(
                    impulse_magnitude * selected.normal_from_second_to_first[0]),
                units::kilogram_metres_per_second(
                    impulse_magnitude * selected.normal_from_second_to_first[1]),
            };
            first.state.momentum = first.state.momentum + impulse_on_first;
            first_ledger.boundary_impulse = first_ledger.boundary_impulse + impulse_on_first;
            result.boundary_impulse = result.boundary_impulse + impulse_on_first;
            result.boundary_angular_impulse +=
                angular_impulse_about_origin(contact_point, impulse_on_first);
        } else {
            if (!selected.second_index) {
                return std::unexpected(SystemError::NumericalOverflow);
            }
            auto& second = result.bodies[*selected.second_index];
            auto& second_ledger = result.particle_impulses[*selected.second_index];
            second_particle = second.state.id;
            const auto first_velocity = world::velocity_of(first.state);
            const auto second_velocity = world::velocity_of(second.state);
            const auto relative_normal_speed =
                units::in_metres_per_second(first_velocity[0] - second_velocity[0]) *
                    selected.normal_from_second_to_first[0] +
                units::in_metres_per_second(first_velocity[1] - second_velocity[1]) *
                    selected.normal_from_second_to_first[1];
            const auto inverse_mass_sum = 1.0 / units::in_kilograms(first.state.rest_mass) +
                                          1.0 / units::in_kilograms(second.state.rest_mass);
            const auto restitution = materials::effective_restitution(
                *responses[selected.first_index],
                *responses[*selected.second_index]);
            const auto impulse_magnitude =
                -(1.0 + restitution) * relative_normal_speed / inverse_mass_sum;
            const auto loss = analytic_restitution_energy_loss(
                impulse_magnitude,
                -relative_normal_speed,
                restitution);
            if (!loss) {
                return std::unexpected(loss.error());
            }
            energy_loss = *loss;
            impulse_on_first = math::Vector<2, units::Momentum>{
                units::kilogram_metres_per_second(
                    impulse_magnitude * selected.normal_from_second_to_first[0]),
                units::kilogram_metres_per_second(
                    impulse_magnitude * selected.normal_from_second_to_first[1]),
            };
            first.state.momentum = first.state.momentum + impulse_on_first;
            second.state.momentum = second.state.momentum - impulse_on_first;
            first_ledger.internal_impulse = first_ledger.internal_impulse + impulse_on_first;
            second_ledger.internal_impulse = second_ledger.internal_impulse - impulse_on_first;
        }
        if (!finite_momentum(impulse_on_first) || !std::isfinite(units::in_joules(energy_loss))) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        result.unresolved_internal_energy_flux += energy_loss;
        if (!std::isfinite(units::in_joules(result.unresolved_internal_energy_flux)) ||
            !std::isfinite(units::in_kilogram_square_metres_per_second(
                result.boundary_angular_impulse)) ||
            !finite_momentum(result.boundary_impulse)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
        result.contacts.push_back(FrictionlessContactSystemEvent2{
            units::seconds(elapsed_seconds),
            selected.kind,
            first.state.id,
            second_particle,
            selected.boundary,
            contact_point,
            selected.normal_from_second_to_first,
            impulse_on_first,
            energy_loss,
        });
        if (!finite_result_state(result)) {
            return std::unexpected(SystemError::NumericalOverflow);
        }
    }
    if (!finite_result_state(result)) {
        return std::unexpected(SystemError::NumericalOverflow);
    }
    return result;
}

}  // namespace principia::solvers
