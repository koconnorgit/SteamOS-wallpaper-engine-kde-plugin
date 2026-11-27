#pragma once

#include "Parameters.hpp"
#include "Type.hpp"
#include "Core/NoCopyMove.hpp"
#include "Core/MapSet.hpp"

namespace wallpaper
{

class Image;

namespace vulkan
{

VkFormat             ToVkType(TextureFormat);
VkSamplerAddressMode ToVkType(TextureWrap);
VkFilter             ToVkType(TextureFilter);

enum class TexUsage
{
    COLOR,
    DEPTH
};

using TexHash = std::size_t;

struct TextureKey {
    i32           width;
    i32           height;
    TexUsage      usage;
    TextureFormat format;
    TextureSample sample;
    uint          mipmap_level { 1 };

    static TexHash HashValue(const TextureKey&);
};

class TextureCache : NoCopy, NoMove {
public:
    TextureCache(const Device&);
    ~TextureCache();

    void Clear();

    std::optional<ExImageParameters> CreateExTex(uint32_t witdh, uint32_t height, VkFormat,
                                                 VkImageTiling);
    ImageSlotsRef                    CreateTex(Image&);
    // the texture made from the image with this key, if there is one already
    std::optional<ImageSlotsRef>     FindTex(const std::string& key);
    // 1x1 transparent black, for samplers that have nothing else to show
    ImageSlotsRef                    FallbackTex();
    // For videos: replace the picture of an RGBA texture made by CreateTex.
    // Write the frame into the buffer the first call returns (it stays valid
    // and mapped), then commit. The commit blocks until the gpu has it.
    uint8_t* UpdateTexBuffer(const std::string& key, usize size);
    bool     UpdateTexCommit(const std::string& key, u32 width, u32 height);

    std::optional<ImageParameters> Query(std::string_view key, TextureKey content_hash,
                                         bool persist = false);

    void MarkShareReady(std::string_view key);

    void RecGenerateMipmaps(vvk::CommandBuffer& cmd, const ImageParameters& image) const;

private:
    std::optional<VmaImageParameters> CreateTex(TextureKey);
    void                              allocateCmd();
    vvk::CommandBuffers               m_tex_cmds;
    vvk::CommandBuffer                m_tex_cmd;

    const Device&                m_device;
    Map<std::string, ImageSlots> m_tex_map;
    struct UpdateBuf {
        VmaBufferParameters buf;
        void*               mapped { nullptr };
    };
    Map<std::string, UpdateBuf> m_update_bufs;

    struct QueryTex {
        idx                index { 0 };
        bool               share_ready { false };
        bool               persist { false };
        TexHash            content_hash;
        VmaImageParameters image;
        Set<std::string>   query_keys;
    };
    std::vector<std::unique_ptr<QueryTex>> m_query_texs;
    Map<std::string, QueryTex*>            m_query_map;
};

} // namespace vulkan
} // namespace wallpaper
