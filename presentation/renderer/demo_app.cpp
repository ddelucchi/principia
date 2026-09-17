#include "demo_app.hpp"

#include "cpu_canvas.hpp"
#include "gpu_window.hpp"
#include "world_view.hpp"

#include <principia/diagnostics/inspection.hpp>
#include <principia/game/progression/learning_graph.hpp>
#include <principia/game/reality_test_001.hpp>
#include <principia/solvers/newtonian_particle_solver.hpp>
#include <principia/units/quantity.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace principia::presentation {
namespace {

[[nodiscard]] int parse_maximum_frames(int argc, char** argv)
{
    if (argc == 1) {
        return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "--frames") {
        const std::string_view text{argv[2]};
        auto maximum_frames = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), maximum_frames);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
            maximum_frames > 0) {
            return maximum_frames;
        }
        throw std::runtime_error("--frames requires a positive integer");
    }
    throw std::runtime_error("usage: principia_demo [--frames positive-integer]");
}

[[nodiscard]] std::string make_title(
    const diagnostics::WorldInspectionFrame2& inspection,
    bool paused,
    bool field_visible,
    bool operator_installed,
    double conservation_residual)
{
    std::string title = "Principia | SPACE pause, N step, F field, G operator, R reset | ";
    title += paused ? "PAUSED" : "RUNNING";
    title += field_visible ? " | perceived:g" : " | perceived:hidden";
    title += operator_installed ? " | operator:on" : " | operator:off";
    char buffer[128]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        " | tick=%llu | residual=%.2e",
        static_cast<unsigned long long>(inspection.tick),
        conservation_residual);
    title += buffer;
    return title;
}

class DemoApp {
public:
    explicit DemoApp(int maximum_frames)
        : maximum_frames_(maximum_frames),
          scenario_(game::make_reality_test_001()),
          operator_node_(scenario_.gravity.operator_graph().ordered_nodes().front())
    {
        const auto learning_graph = game::progression::make_standard_learning_graph();
        const auto access = game::progression::make_reality_test_access(learning_graph);
        may_observe_field_ = access.permits_any_variant(
            game::progression::intervention_targets::effective_gravity_field,
            game::progression::AccessKind::Observe);
        may_modify_field_ = access.permits_any_variant(
            game::progression::intervention_targets::effective_gravity_field,
            game::progression::AccessKind::Modify);
    }

    int run()
    {
        using Clock = std::chrono::steady_clock;

        auto previous = Clock::now();
        while (running_) {
            process_events();

            const auto now = Clock::now();
            const auto frame_seconds = std::chrono::duration<double>(now - previous).count();
            previous = now;
            advance(frame_seconds);
            const diagnostics::InspectionContactContext2 inspection_contact{
                scenario_.materials,
                scenario_.mechanical_responses,
                scenario_.disc_colliders,
            };
            const auto inspection = diagnostics::capture_world_inspection(
                scenario_.world,
                scenario_.gravity,
                inspection_contact);
            if (!inspection) {
                throw std::runtime_error("committed world inspection failed");
            }
            update_title(frame_seconds, *inspection);
            render(*inspection);

            ++rendered_frames_;
            if (maximum_frames_ != 0 && rendered_frames_ >= maximum_frames_) {
                running_ = false;
            }
            SDL_Delay(1);
        }
        return 0;
    }

private:
    static constexpr double fixed_step_seconds = 1.0 / 120.0;

    void process_events()
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                process_key(event.key.key);
            }
        }
    }

    void process_key(SDL_Keycode key)
    {
        switch (key) {
        case SDLK_ESCAPE:
            running_ = false;
            break;
        case SDLK_SPACE:
            paused_ = !paused_;
            break;
        case SDLK_N:
            single_step_ = paused_;
            break;
        case SDLK_F:
            if (may_observe_field_) {
                field_visible_ = !field_visible_;
            }
            break;
        case SDLK_G:
            if (may_modify_field_) {
                if (operator_installed_) {
                    operator_installed_ = !scenario_.gravity.remove_operator(operator_node_.id);
                } else {
                    operator_installed_ = scenario_.gravity.install_operator(operator_node_);
                }
            }
            break;
        case SDLK_R:
            scenario_ = game::make_reality_test_001();
            operator_installed_ = true;
            conservation_residual_ = 0.0;
            accumulator_ = 0.0;
            break;
        default:
            break;
        }
    }

    void advance(double frame_seconds)
    {
        if (!paused_) {
            accumulator_ += std::min(frame_seconds, 0.25);
        }
        if (single_step_) {
            accumulator_ = fixed_step_seconds;
            single_step_ = false;
        }
        while (accumulator_ >= fixed_step_seconds) {
            const solvers::NewtonianContactEnvironment2 contact{
                scenario_.materials,
                scenario_.mechanical_responses,
                scenario_.disc_colliders,
            };
            const auto step = solver_.advance(
                scenario_.world,
                scenario_.gravity,
                contact,
                fixed_step_);
            if (!step) {
                throw std::runtime_error("authoritative simulation step failed");
            }
            conservation_residual_ = step->conservation.maximum_normalized_residual();
            accumulator_ -= fixed_step_seconds;
        }
    }

    void update_title(double frame_seconds, const diagnostics::WorldInspectionFrame2& inspection)
    {
        title_timer_ += frame_seconds;
        if (title_timer_ >= 0.1) {
            const auto title = make_title(
                inspection, paused_, field_visible_, operator_installed_, conservation_residual_);
            SDL_SetWindowTitle(gpu_window_.window(), title.c_str());
            title_timer_ = 0.0;
        }
    }

    void render(const diagnostics::WorldInspectionFrame2& inspection)
    {
        draw_scene(
            canvas_,
            inspection,
            scenario_.gravity,
            scenario_.roles,
            operator_node_,
            operator_installed_,
            field_visible_,
            paused_);
        gpu_window_.present(canvas_.pixels());
    }

    const SdlLifetime sdl_;
    GpuWindow gpu_window_;
    CpuCanvas canvas_;
    int maximum_frames_{};
    game::RealityTest001Scenario scenario_;
    operators::GravityOperatorNode operator_node_;
    solvers::NewtonianParticleSolver2 solver_;
    units::Duration fixed_step_{units::seconds(fixed_step_seconds)};
    int rendered_frames_{};
    double title_timer_{1.0};
    double accumulator_{};
    double conservation_residual_{};
    bool may_observe_field_{};
    bool may_modify_field_{};
    bool field_visible_{};
    bool operator_installed_{true};
    bool paused_{};
    bool single_step_{};
    bool running_{true};
};

}  // namespace

int run_demo(int argc, char** argv)
{
    try {
        DemoApp app{parse_maximum_frames(argc, argv)};
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "Principia presentation failed: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace principia::presentation
