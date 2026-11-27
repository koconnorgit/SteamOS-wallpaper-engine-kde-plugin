#pragma once
#include <utility>
#include <vector>
#include "Core/NoCopyMove.hpp"
#include "Instance.hpp"
#include "Parameters.hpp"
#include "vk_mem_alloc.h"

namespace wallpaper
{
namespace vulkan
{

class Device;
class StagingBuffer;

class StagingBufferRef {
public:
    VkDeviceSize size { 0 };
    VkDeviceSize offset { 0 };

    operator bool() const { return m_allocation != VK_NULL_HANDLE; }

private:
    friend class StagingBuffer;
    VmaVirtualAllocation m_allocation {};
    size_t               m_virtual_index { 0 };
};

class StagingBuffer : NoCopy, NoMove {
public:
    StagingBuffer(const Device&, VkDeviceSize size, VkBufferUsageFlags);
    ~StagingBuffer();

    bool allocate();
    void destroy();

    bool allocateSubRef(VkDeviceSize size, StagingBufferRef&, VkDeviceSize alignment = 1);
    void unallocateSubRef(const StagingBufferRef&);
    bool writeToBuf(const StagingBufferRef&, std::span<uint8_t>, size_t offset = 0);
    bool fillBuf(const StagingBufferRef& ref, size_t offset, size_t size, uint8_t c);

    // Makes sure gpuBuf() is valid. Call before recording commands that bind
    // it, when recordUpload comes after them.
    bool ensureGpuBuf();
    bool recordUpload(vvk::CommandBuffer&);

    VkBuffer gpuBuf() const;

private:
    struct VirtualBlock {
        VmaVirtualBlock handle {};
        bool            enabled { false };
        size_t          index { 0 };
        VkDeviceSize    offset { 0 };
        VkDeviceSize    size { 0 };
    };

    VkResult      mapStageBuf();
    VirtualBlock* newVirtualBlock(VkDeviceSize);
    bool          increaseBuf(VkDeviceSize);
    void          markDirty(VkDeviceSize begin, VkDeviceSize end);

    const Device& m_device;
    VkDeviceSize  m_size_step;

    VkBufferUsageFlags m_usage;

    void*                     m_stage_raw { nullptr };
    std::vector<VirtualBlock> m_virtual_blocks {};

    VmaBufferParameters m_stage_buf;
    VmaBufferParameters m_gpu_buf;

    // Written since the last upload, as [begin, end) ranges. Only these are
    // copied to the gpu buffer.
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> m_dirty;
    // the gpu buffer is new and needs everything
    bool m_upload_all { false };
};

} // namespace vulkan
} // namespace wallpaper
