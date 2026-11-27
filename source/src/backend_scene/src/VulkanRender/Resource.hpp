#pragma once
#include "Core/NoCopyMove.hpp"
#include "Vulkan/StagingBuffer.hpp"
#include <memory>

namespace wallpaper
{
namespace vulkan
{

struct RenderingResources {
    vvk::CommandBuffer command;

    vvk::Semaphore sem_swap_wait_image;
    vvk::Semaphore sem_swap_finish;
    vvk::Fence     fence_frame;

    StagingBuffer* vertex_buf;
    StagingBuffer* dyn_buf;

    // Set by a pass when this frame differs from the last one in a way that
    // doesn't show as bytes written to dyn_buf: another sprite frame bound, a
    // different number of vertices drawn. See drawFrameOffscreen.
    bool frame_changed { true };
};
} // namespace vulkan
} // namespace wallpaper
