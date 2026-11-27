#pragma once
#include <unordered_map>
#include "VulkanPass.hpp"
#include <string>

#include "Vulkan/Device.hpp"
#include "Vulkan/StagingBuffer.hpp"
#include "Vulkan/GraphicsPipeline.hpp"

#include "Scene/Scene.h"
#include "SpecTexs.hpp"

namespace wallpaper
{
namespace vulkan
{

class FinPass : public VulkanPass {
public:
    struct Desc {
        // in
        const std::string_view result { SpecTex_Default };
        VkFormat               present_format;
        VkImageLayout          present_layout;
        uint32_t               present_queue_index;

        // prepared
        ImageParameters vk_result;
        ImageParameters vk_present;
        VkClearValue    clear_value;

        StagingBufferRef   vertex_buf;
        // by the image view they render to
        std::unordered_map<VkImageView, vvk::Framebuffer> fbs;
        PipelineParameters pipeline;
    };

    FinPass(const Desc&);
    virtual ~FinPass();

    void setPresent(ImageParameters);
    void setPresentLayout(VkImageLayout);
    void setPresentFormat(VkFormat);
    void setPresentQueueIndex(uint32_t);

    void prepare(Scene&, const Device&, RenderingResources&) override;
    void execute(const Device&, RenderingResources&) override;
    void destory(const Device&, RenderingResources&) override;

private:
    Desc m_desc;
};

} // namespace vulkan
} // namespace wallpaper
