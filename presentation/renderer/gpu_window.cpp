#include "gpu_window.hpp"

#include "cpu_canvas.hpp"

#include <SDL3/SDL_main.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace principia::presentation {

void SdlWindowDeleter::operator()(SDL_Window* window) const noexcept
{
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
}

void SdlGpuDeviceDeleter::operator()(SDL_GPUDevice* device) const noexcept
{
    if (device != nullptr) {
        SDL_DestroyGPUDevice(device);
    }
}

SdlLifetime::SdlLifetime()
{
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }
}

SdlLifetime::~SdlLifetime()
{
    SDL_Quit();
}

GpuWindow::GpuWindow()
    : window_(SDL_CreateWindow("Principia | Reality Test 001", 960, 540, SDL_WINDOW_RESIZABLE)),
      device_(SDL_CreateGPUDevice(
          static_cast<SDL_GPUShaderFormat>(
              SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL),
          true,
          nullptr))
{
    if (!window_ || !device_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_ClaimWindowForGPUDevice(device_.get(), window_.get())) {
        throw std::runtime_error(SDL_GetError());
    }
    claimed_ = true;

    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = canvas_width;
    texture_info.height = canvas_height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(device_.get(), &texture_info);

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = canvas_width * canvas_height * sizeof(std::uint32_t);
    upload_ = SDL_CreateGPUTransferBuffer(device_.get(), &transfer_info);
    if (texture_ == nullptr || upload_ == nullptr) {
        const std::string error = SDL_GetError();
        release_gpu_resources();
        throw std::runtime_error(error);
    }
}

GpuWindow::~GpuWindow()
{
    release_gpu_resources();
}

SDL_Window* GpuWindow::window() const noexcept
{
    return window_.get();
}

void GpuWindow::release_gpu_resources() noexcept
{
    if (device_) {
        static_cast<void>(SDL_WaitForGPUIdle(device_.get()));
        if (upload_ != nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_.get(), upload_);
            upload_ = nullptr;
        }
        if (texture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_.get(), texture_);
            texture_ = nullptr;
        }
        if (claimed_) {
            SDL_ReleaseWindowFromGPUDevice(device_.get(), window_.get());
            claimed_ = false;
        }
    }
}

void GpuWindow::present(std::span<const std::uint32_t> pixels)
{
    if (pixels.size() != static_cast<std::size_t>(canvas_width * canvas_height)) {
        throw std::runtime_error("invalid CPU canvas extent");
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_.get(), upload_, true);
    if (mapped == nullptr) {
        throw std::runtime_error(SDL_GetError());
    }
    std::memcpy(mapped, pixels.data(), pixels.size_bytes());
    SDL_UnmapGPUTransferBuffer(device_.get(), upload_);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device_.get());
    if (command_buffer == nullptr) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (copy_pass == nullptr) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        throw std::runtime_error(SDL_GetError());
    }
    const SDL_GPUTextureTransferInfo source{upload_, 0, canvas_width, canvas_height};
    const SDL_GPUTextureRegion destination{texture_, 0, 0, 0, 0, 0, canvas_width, canvas_height, 1};
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            command_buffer, window_.get(), &swapchain_texture, &width, &height)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        throw std::runtime_error(SDL_GetError());
    }
    if (swapchain_texture != nullptr) {
        SDL_GPUBlitInfo blit{};
        blit.source = SDL_GPUBlitRegion{texture_, 0, 0, 0, 0, canvas_width, canvas_height};
        blit.destination = SDL_GPUBlitRegion{swapchain_texture, 0, 0, 0, 0, width, height};
        blit.load_op = SDL_GPU_LOADOP_CLEAR;
        blit.clear_color = SDL_FColor{0.01F, 0.01F, 0.02F, 1.0F};
        blit.flip_mode = SDL_FLIP_NONE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(command_buffer, &blit);
    }
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        throw std::runtime_error(SDL_GetError());
    }
}

}  // namespace principia::presentation
