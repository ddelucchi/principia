#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <span>

namespace principia::presentation {

struct SdlWindowDeleter {
    void operator()(SDL_Window* window) const noexcept;
};

struct SdlGpuDeviceDeleter {
    void operator()(SDL_GPUDevice* device) const noexcept;
};

class SdlLifetime {
public:
    SdlLifetime();
    ~SdlLifetime();

    SdlLifetime(const SdlLifetime&) = delete;
    SdlLifetime& operator=(const SdlLifetime&) = delete;
    SdlLifetime(SdlLifetime&&) = delete;
    SdlLifetime& operator=(SdlLifetime&&) = delete;
};

class GpuWindow {
public:
    GpuWindow();
    ~GpuWindow();

    GpuWindow(const GpuWindow&) = delete;
    GpuWindow& operator=(const GpuWindow&) = delete;
    GpuWindow(GpuWindow&&) = delete;
    GpuWindow& operator=(GpuWindow&&) = delete;

    [[nodiscard]] SDL_Window* window() const noexcept;
    void present(std::span<const std::uint32_t> pixels);

private:
    using WindowHandle = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
    using GpuDeviceHandle = std::unique_ptr<SDL_GPUDevice, SdlGpuDeviceDeleter>;

    void release_gpu_resources() noexcept;

    WindowHandle window_;
    GpuDeviceHandle device_;
    SDL_GPUTexture* texture_{};
    SDL_GPUTransferBuffer* upload_{};
    bool claimed_{false};
};

}  // namespace principia::presentation
