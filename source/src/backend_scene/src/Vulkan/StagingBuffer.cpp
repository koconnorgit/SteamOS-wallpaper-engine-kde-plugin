#include "StagingBuffer.hpp"
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>
#include "Util.hpp"
#include "Device.hpp"

using namespace wallpaper::vulkan;

#define CHECK_REF(ref, act)                                                  \
    if (! ref) {                                                             \
        LOG_ERROR("stage ref not available, index %d", ref.m_virtual_index); \
        { act; }                                                             \
    }

StagingBuffer::StagingBuffer(const Device& d, VkDeviceSize size, VkBufferUsageFlags usage)
    : m_device(d), m_size_step(size), m_usage(usage) {}
StagingBuffer::~StagingBuffer() {}

namespace
{
std::optional<VmaBufferParameters> CreateGpuBuffer(VmaAllocator allocator, VkBufferUsageFlags usage,
                                                   std::size_t size) {
    do {
        VmaBufferParameters buffer;
        VkBufferCreateInfo  ci {
             .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
             .pNext = nullptr,
             .size  = size,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        };
        buffer.req_size                  = ci.size;
        VmaAllocationCreateInfo vma_info = {};
        vma_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
        VVK_CHECK_ACT(break, vvk::CreateBuffer(allocator, ci, vma_info, buffer.handle));
        return buffer;
    } while (false);
    return std::nullopt;
}

void RecordCopyBuffer(const BufferParameters& dst_buf, const BufferParameters& src_buf,
                      vvk::CommandBuffer& cmd, std::vector<VkBufferCopy>& regions) {
    cmd.CopyBuffer(src_buf.handle, dst_buf.handle, regions);

    VkBufferMemoryBarrier in_bar {
        .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext         = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT,
        .buffer        = dst_buf.handle,
        .offset        = 0,
        .size          = VK_WHOLE_SIZE,
    };
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        in_bar);
}
} // namespace

StagingBuffer::VirtualBlock* StagingBuffer::newVirtualBlock(VkDeviceSize nsize) {
    auto it = std::find_if(m_virtual_blocks.begin(), m_virtual_blocks.end(), [nsize](auto& b) {
        return ! b.enabled && b.size >= nsize;
    });
    if (it == std::end(m_virtual_blocks)) {
        VkDeviceSize offset = m_virtual_blocks.empty()
                                  ? 0
                                  : m_virtual_blocks.back().offset + m_virtual_blocks.back().size;

        m_virtual_blocks.push_back({});
        it         = m_virtual_blocks.end() - 1;
        // Grow by half of what there is, at least. Every growth moves the whole
        // buffer, so growing by a fixed step costs more each time.
        it->size   = std::max({ nsize, m_size_step, offset / 2 });
        it->index  = (size_t)std::distance(m_virtual_blocks.begin(), it);
        it->offset = offset;
    }
    auto& block = *it;

    VmaVirtualBlockCreateInfo blockCreateInfo = {};
    blockCreateInfo.size                      = block.size;

    VVK_CHECK_ACT(return nullptr, vmaCreateVirtualBlock(&blockCreateInfo, &block.handle));
    block.enabled = true;

    LOG_INFO("new buffer block(%p), size: %d, index: %d / %d",
             this,
             block.size,
             block.index,
             m_virtual_blocks.size());
    return &block;
}
bool StagingBuffer::increaseBuf(VkDeviceSize nsize) {
    if (m_stage_raw == nullptr) {
        VVK_CHECK_BOOL_RE(mapStageBuf());
    }
    const auto oldsize = m_stage_buf.req_size;
    const auto newsize = oldsize + nsize;

    VmaBufferParameters bigger;
    if (! CreateStagingBuffer(m_device.vma_allocator(), newsize, bigger, true)) return false;
    void* bigger_raw { nullptr };
    VVK_CHECK_BOOL_RE(bigger.handle.MapMemory(&bigger_raw));
    memcpy(bigger_raw, m_stage_raw, oldsize);

    m_stage_buf.handle.UnMapMemory();
    m_stage_buf = std::move(bigger);
    m_stage_raw = bigger_raw;

    // the gpu buffer is too small now; the next upload makes a new one and fills it
    m_gpu_buf.handle = nullptr;
    m_dirty.clear();
    LOG_INFO("increase buffer size: %d", nsize);
    return true;
}

bool StagingBuffer::allocate() {
    if (! CreateStagingBuffer(m_device.vma_allocator(), m_size_step, m_stage_buf, true)) return false;
    // stays mapped for as long as the buffer lives
    VVK_CHECK_BOOL_RE(m_stage_buf.handle.MapMemory(&m_stage_raw));
    m_dirty.clear();
    auto* block = newVirtualBlock(m_size_step);
    return block != nullptr;
}

void StagingBuffer::markDirty(VkDeviceSize begin, VkDeviceSize end) {
    if (end <= begin) return;
    // writes usually come in ascending order, one allocation after the other
    if (! m_dirty.empty() && begin <= m_dirty.back().second && end >= m_dirty.back().first) {
        m_dirty.back().first  = std::min(m_dirty.back().first, begin);
        m_dirty.back().second = std::max(m_dirty.back().second, end);
        return;
    }
    m_dirty.emplace_back(begin, end);
}

void StagingBuffer::destroy() {
    if (m_stage_raw != nullptr) {
        m_stage_buf.handle.UnMapMemory();
        m_stage_raw = nullptr;
    }
    m_dirty.clear();
    for (auto& block : m_virtual_blocks) {
        if (block.enabled) {
            vmaClearVirtualBlock(block.handle);
            vmaDestroyVirtualBlock(block.handle);
        }
    }
    m_virtual_blocks.clear();

    m_stage_buf = {};
    m_gpu_buf   = {};
}

bool StagingBuffer::allocateSubRef(VkDeviceSize size, StagingBufferRef& ref,
                                   VkDeviceSize alignment) {
    VmaVirtualAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.size                           = size;
    allocCreateInfo.alignment                      = alignment;

    VmaVirtualAllocation allocation;
    VkDeviceSize         offset;

    auto setRef = [&offset, &allocation, size](StagingBufferRef& ref, VirtualBlock& block) {
        ref.size   = size;
        ref.offset = offset + block.offset;

        ref.m_allocation    = allocation;
        ref.m_virtual_index = block.index;
    };

    for (auto& block : m_virtual_blocks) {
        if (block.enabled && block.size >= size) {
            if (auto res = vmaVirtualAllocate(block.handle, &allocCreateInfo, &allocation, &offset);
                res == VK_SUCCESS) {
                setRef(ref, block);
                return true;
            }
        }
    }

    auto  old_block_num = m_virtual_blocks.size();
    auto* p_block       = newVirtualBlock(size);
    if (p_block == nullptr) return false;

    auto& block = *p_block;
    if (old_block_num < m_virtual_blocks.size()) {
        if (! increaseBuf(block.size)) {
            auto& block = m_virtual_blocks.back();
            vmaClearVirtualBlock(block.handle);
            vmaDestroyVirtualBlock(block.handle);
            m_virtual_blocks.pop_back();
            LOG_ERROR("increase buf failed, pop_back block, current: %d", m_virtual_blocks.size());
            return false;
        }
    }
    VVK_CHECK_BOOL_RE(vmaVirtualAllocate(block.handle, &allocCreateInfo, &allocation, &offset));
    setRef(ref, block);
    return true;
}
void StagingBuffer::unallocateSubRef(const StagingBufferRef& ref) {
    CHECK_REF(ref, ;);
    if (ref.m_virtual_index < m_virtual_blocks.size()) {
        auto& block = m_virtual_blocks[ref.m_virtual_index];
        vmaVirtualFree(block.handle, ref.m_allocation);
        if (block.enabled && vmaIsVirtualBlockEmpty(block.handle)) {
            vmaDestroyVirtualBlock(block.handle);
            block.handle  = VK_NULL_HANDLE;
            block.enabled = false;
        }
    } else {
        LOG_ERROR("unallocate stagingbuffer failed: wrong index %d", ref.m_virtual_index);
    }
}

VkResult StagingBuffer::mapStageBuf() { return m_stage_buf.handle.MapMemory(&m_stage_raw); }

bool StagingBuffer::writeToBuf(const StagingBufferRef& ref, std::span<uint8_t> data,
                               size_t offset) {
    CHECK_REF(ref, return false);

    if (m_stage_raw == nullptr) {
        mapStageBuf();
    }
    VkDeviceSize size = std::min(ref.size - offset, data.size());
    uint8_t*     raw  = (uint8_t*)m_stage_raw;
    // Most uniforms are written every frame with the value they already have.
    // Leaving those alone keeps them out of the upload, and lets a frame in
    // which nothing changed be recognised as such.
    if (size == 0 || std::memcmp(raw + ref.offset + offset, data.data(), size) == 0) return true;
    std::copy(data.begin(), data.begin() + size, raw + ref.offset + offset);
    markDirty(ref.offset + offset, ref.offset + offset + size);
    return true;
}

bool StagingBuffer::fillBuf(const StagingBufferRef& ref, size_t offset, size_t size, uint8_t c) {
    CHECK_REF(ref, return false);

    if (m_stage_raw == nullptr) {
        mapStageBuf();
    }
    VkDeviceSize size_     = std::min(ref.size - offset, size);
    uint8_t*     raw       = (uint8_t*)m_stage_raw;
    uint8_t*     raw_begin = raw + ref.offset + offset;
    std::fill(raw_begin, raw_begin + size_, c);
    markDirty(ref.offset + offset, ref.offset + offset + size_);
    return true;
}

bool StagingBuffer::ensureGpuBuf() {
    if (m_gpu_buf.handle) return true;
    if (auto opt = CreateGpuBuffer(m_device.vma_allocator(), m_usage, m_stage_buf.req_size);
        opt.has_value()) {
        m_gpu_buf = std::move(opt.value());
    } else
        return false;
    m_upload_all = true;
    return true;
}

bool StagingBuffer::recordUpload(vvk::CommandBuffer& cmd) {
    std::vector<VkBufferCopy> regions;
    if (! ensureGpuBuf()) return false;
    if (m_upload_all) {
        // a new gpu buffer has nothing in it yet
        m_upload_all = false;
        regions.push_back({ 0, 0, m_stage_buf.req_size });
    } else {
        if (m_dirty.empty()) return true;
        // Merge ranges that touch or nearly touch. A frame typically writes a
        // few hundred small uniform blocks and some particle meshes.
        constexpr VkDeviceSize gap { 4096 };
        std::sort(m_dirty.begin(), m_dirty.end());
        for (const auto& [begin, end] : m_dirty) {
            if (! regions.empty() &&
                begin <= regions.back().srcOffset + regions.back().size + gap) {
                auto& last = regions.back();
                last.size  = std::max(last.srcOffset + last.size, end) - last.srcOffset;
            } else {
                regions.push_back({ begin, begin, end - begin });
            }
        }
        // not worth a long list: fall back to one range over all of them
        if (regions.size() > 256) {
            const auto begin = regions.front().srcOffset;
            const auto end   = regions.back().srcOffset + regions.back().size;
            regions.assign(1, { begin, begin, end - begin });
        }
    }
    m_dirty.clear();

    VVK_CHECK_BOOL_RE(vmaFlushAllocation(
        m_device.vma_allocator(), m_stage_buf.handle.Allocation(), 0, VK_WHOLE_SIZE));
    RecordCopyBuffer(m_gpu_buf, m_stage_buf, cmd, regions);
    return true;
}

VkBuffer StagingBuffer::gpuBuf() const { return *m_gpu_buf.handle; }
