#pragma once
#include "Instance.hpp"
#include "Parameters.hpp"

#include "vvk/vma_wrapper.hpp"

#include "vk_mem_alloc.h"

namespace wallpaper
{
namespace vulkan
{

// `readable`: the caller reads the mapped memory back, so ask for cached memory.
// Without it the driver may hand out write-combined memory, which is fine to
// fill and very slow to read.
inline bool CreateStagingBuffer(VmaAllocator allocator, std::size_t size,
                                VmaBufferParameters& buffer, bool readable = false) {
    VkBufferCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size  = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    buffer.req_size = ci.size;

    VmaAllocationCreateInfo vma_info = {};
    vma_info.usage                   = VMA_MEMORY_USAGE_CPU_ONLY;
    if (readable) vma_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VVK_CHECK_BOOL_RE(vvk::CreateBuffer(allocator, ci, vma_info, buffer.handle));
    return true;
}
} // namespace vulkan
} // namespace wallpaper
