#include "WPTexImageParser.hpp"

#include "Type.hpp"
#include "WPCommon.hpp"
#include <cstdint>
#include <lz4.h>

#include "SpriteAnimation.hpp"
#include "Utils/Algorism.h"
#include "Fs/VFS.h"
#include "Utils/BitFlags.hpp"
#include "Utils/LoadStats.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
#include <cstring>
#include <memory>
#include <algorithm>
#include <iostream>

using namespace wallpaper;

enum class WPTexFlagEnum : uint32_t
{
    // true for no bilinear
    noInterpolation = 0,
    // true for no repeat
    clampUVs = 1,
    sprite   = 2,

    compo1 = 20,
    compo2 = 21,
    compo3 = 22
};
using WPTexFlags = BitFlags<WPTexFlagEnum>;

namespace
{
// bytes a width x height level takes in a raw (non container) format
i64 ExpectedSize(TextureFormat format, i32 w, i32 h) {
    const i64 blocks = ((i64)w + 3) / 4 * (((i64)h + 3) / 4);
    switch (format) {
    case TextureFormat::BC1: return blocks * 8;
    case TextureFormat::BC2:
    case TextureFormat::BC3: return blocks * 16;
    case TextureFormat::RG8: return (i64)w * h * 2;
    case TextureFormat::R8: return (i64)w * h;
    case TextureFormat::RGBA8:
    default: return (i64)w * h * 4;
    }
}

TextureFormat ToTexFormate(int type) {
    /*
        type
        RGBA8888 = 0,
        DXT5 = 4,
        DXT3 = 6,
        DXT1 = 7,
        RG88 = 8,
        R8 = 9,
    */
    switch (type) {
    case 0: return TextureFormat::RGBA8;
    case 4: return TextureFormat::BC3;
    case 6: return TextureFormat::BC2;
    case 7: return TextureFormat::BC1;
    case 8: return TextureFormat::RG8;
    case 9: return TextureFormat::R8;
    default:
        LOG_ERROR("ERROR::ToTexFormate Unkown image type: %d", type);
        return TextureFormat::RGBA8;
    }
}
void LoadHeader(fs::IBinaryStream& file, ImageHeader& header) {
    header.extraHeader["texv"].val = ReadTexVesion(file);
    header.extraHeader["texi"].val = ReadTexVesion(file);

    header.format = ToTexFormate(file.ReadInt32());
    WPTexFlags flags(file.ReadUint32());
    {
        header.isSprite     = flags[WPTexFlagEnum::sprite];
        header.sample.wrapS = header.sample.wrapT =
            flags[WPTexFlagEnum::clampUVs] ? TextureWrap::CLAMP_TO_EDGE : TextureWrap::REPEAT;
        header.sample.minFilter = header.sample.magFilter =
            flags[WPTexFlagEnum::noInterpolation] ? TextureFilter::NEAREST : TextureFilter::LINEAR;
        header.extraHeader["compo1"].val = flags[WPTexFlagEnum::compo1];
        header.extraHeader["compo2"].val = flags[WPTexFlagEnum::compo2];
        header.extraHeader["compo3"].val = flags[WPTexFlagEnum::compo3];
    }

    /*
        picture:
        width, height --> pow of 2 (tex size)
        mapw, maph    --> pic size
        mips
        mipw,miph     --> pow of 2

        sprites:
        width, height --> piece of sprite sheet
        mapw, maph    --> same
        1 mip
        mipw,mimp     --> tex size
    */

    header.width  = file.ReadInt32();
    header.height = file.ReadInt32();
    // in sprite this mean one pic
    header.mapWidth  = file.ReadInt32();
    header.mapHeight = file.ReadInt32();

    file.ReadInt32(); // unknown

    header.extraHeader["texb"].val = ReadTexVesion(file);

    header.count = file.ReadInt32();

    const i32 texb = header.extraHeader["texb"].val;
    if (texb == 3) {
        header.type = static_cast<ImageType>(file.ReadInt32());
    } else if (texb >= 4) {
        // TEXB0004 adds a video flag after the container image type
        header.type                        = static_cast<ImageType>(file.ReadInt32());
        header.extraHeader["isvideo"].val = file.ReadInt32() == 1 ? 1 : 0;
    }
}

// TEXB0004 video textures prefix every mipmap with extra fields,
// still images keep the TEXB0003 layout
bool SkipMipmapV4Header(fs::IBinaryStream& file, ImageHeader& header) {
    if (header.extraHeader["texb"].val < 4 || header.extraHeader["isvideo"].val != 1) return true;
    file.ReadInt32();
    file.ReadInt32();
    // condition json, null terminated
    char c { 0 };
    do {
        if (file.Read(&c, 1) != 1) return false;
    } while (c != '\0');
    file.ReadInt32();
    return true;
}

void SetHeaderPow2(ImageHeader& header, i32 mip_0_w, i32 mip_0_h) {
    header.mipmap_pow2   = algorism::IsPowOfTwo((u32)mip_0_w) || algorism::IsPowOfTwo((u32)mip_0_h);
    header.mipmap_larger = mip_0_w * mip_0_h > header.mapWidth * header.mapHeight;
}

} // namespace

std::shared_ptr<Image> WPTexImageParser::Parse(const std::string& name) {
    LoadStats::Timer timer(LoadStats::get().tex_decode_ms);
    LoadStats::get().tex_decoded++;
    std::string            path    = "/assets/materials/" + name + ".tex";
    std::shared_ptr<Image> img_ptr = std::make_shared<Image>();
    auto&                  img     = *img_ptr;
    img.key                        = name;
    // std::ifstream file = fs::GetFileFstream(vfs, path);
    auto pfile = m_vfs->Open(path);
    if (! pfile) return nullptr;
    auto& file     = *pfile;
    LoadHeader(file, img.header);

    if (img.header.extraHeader["isvideo"].val == 1) {
        LOG_ERROR("video texture \"%s\" is not supported", name.c_str());
        return nullptr;
    }

    // Everything below is sized by numbers read from the file, so each one is
    // checked against what the file can actually hold before it is used.
    auto remain = [&file]() {
        return file.Size() - file.Tell();
    };
    auto bad = [&name](const char* what) {
        LOG_ERROR("texture \"%s\": %s", name.c_str(), what);
        return nullptr;
    };
    constexpr i32 max_images { 4096 }, max_mipmaps { 16 }, max_side { 32768 };
    constexpr i32 max_decompressed { 1 << 30 };

    // image
    i32 _image_count = img.header.count;
    if (_image_count < 0 || _image_count > max_images || _image_count > remain() / 4)
        return bad("wrong image count");
    usize image_count = (usize)_image_count;

    const bool is_container =
        img.header.extraHeader["texb"].val >= 3 && img.header.type != ImageType::UNKNOWN;

    img.slots.resize(image_count);
    for (usize i_image = 0; i_image < image_count; i_image++) {
        auto& img_slot = img.slots[i_image];
        auto& mipmaps  = img_slot.mipmaps;

        i32 _mipmap_count = file.ReadInt32();
        if (_mipmap_count < 0 || _mipmap_count > max_mipmaps) return bad("wrong mipmap count");
        usize mipmap_count = (usize)_mipmap_count;
        mipmaps.resize(mipmap_count);
        // load image
        for (usize i_mipmap = 0; i_mipmap < mipmap_count; i_mipmap++) {
            auto& mipmap  = mipmaps.at(i_mipmap);
            mipmap.width  = file.ReadInt32();
            mipmap.height = file.ReadInt32();

            bool    LZ4_compressed    = false;
            int32_t decompressed_size = 0;
            // check compress
            if (img.header.extraHeader["texb"].val > 1) {
                LZ4_compressed    = file.ReadInt32() == 1;
                decompressed_size = file.ReadInt32();
            }

            i32 src_size = file.ReadInt32();
            if (src_size <= 0 || mipmap.width <= 0 || mipmap.height <= 0 ||
                mipmap.width > max_side || mipmap.height > max_side || decompressed_size < 0 ||
                decompressed_size > max_decompressed || src_size > remain())
                return bad("wrong mipmap size");

            auto result = std::make_unique<uint8_t[]>((usize)src_size);
            if (file.Read(result.get(), (usize)src_size) != (usize)src_size)
                return bad("file is truncated");

            // is LZ4 compress
            if (LZ4_compressed) {
                if (decompressed_size == 0) return bad("wrong mipmap size");
                auto decompressed = std::make_unique<uint8_t[]>((usize)decompressed_size);
                int  load_size    = LZ4_decompress_safe((const char*)result.get(),
                                                        (char*)decompressed.get(),
                                                        src_size,
                                                        decompressed_size);
                if (load_size < decompressed_size) return bad("lz4 decompress failed");
                result   = std::move(decompressed);
                src_size = decompressed_size;
            }
            // is image container
            if (is_container) {
                int32_t w { 0 }, h { 0 }, n { 0 };
                auto*   data = stbi_load_from_memory(result.get(), src_size, &w, &h, &n, 4);
                if (data == nullptr || w <= 0 || h <= 0 || w > max_side || h > max_side) {
                    if (data != nullptr) stbi_image_free(data);
                    LOG_ERROR("decode image container of \"%s\" failed, type: %d",
                              name.c_str(),
                              (int)img.header.type);
                    return nullptr;
                }
                mipmap.data = ImageDataPtr((uint8_t*)data, [](uint8_t* data) {
                    stbi_image_free((unsigned char*)data);
                });
                // the decoded size is what the pixels really are
                mipmap.width  = w;
                mipmap.height = h;
                mipmap.size   = (isize)w * h * 4;
            } else {
                // an mp4 stored as a texture; these have no pixel data to upload
                if (src_size >= 8 && std::memcmp(result.get() + 4, "ftyp", 4) == 0)
                    return bad("is a video texture, which is not supported");
                // the gpu copies width x height texels out of this buffer
                if ((i64)src_size < ExpectedSize(img.header.format, mipmap.width, mipmap.height))
                    return bad("mipmap data is smaller than its size needs");
                mipmap.size = src_size;
                mipmap.data = ImageDataPtr(result.release(), [](uint8_t* data) {
                    delete[] data;
                });
            }

            if (i_mipmap == 0) {
                img_slot.width  = mipmap.width;
                img_slot.height = mipmap.height;
                SetHeaderPow2(img.header, mipmap.width, mipmap.height);
            }
        }

        // A mip level has to be the size the gpu expects for it. Stop the chain
        // at the first level that isn't; the levels before it still work.
        for (usize i = 1; i < mipmaps.size(); i++) {
            if (mipmaps[i].width != std::max(1, img_slot.width >> i) ||
                mipmaps[i].height != std::max(1, img_slot.height >> i)) {
                LOG_INFO("texture \"%s\": mipmap %d has an unexpected size, dropping it and above",
                         name.c_str(),
                         (int)i);
                mipmaps.resize(i);
                break;
            }
        }
    }
    return img_ptr;
}

ImageHeader WPTexImageParser::ParseHeader(const std::string& name) {
    LoadStats::Timer timer(LoadStats::get().tex_header_ms);
    ImageHeader header;
    std::string path  = "/assets/materials/" + name + ".tex";
    auto        pfile = m_vfs->Open(path);
    if (! pfile) return header;
    auto& file = *pfile;

    LoadHeader(file, header);
    if (header.count < 0) return header;

    usize image_count = (usize)header.count;

    // load sprite info
    if (header.isSprite) {
        auto remain = [&file]() {
            return file.Size() - file.Tell();
        };
        // A sprite sheet that doesn't add up is shown as a plain texture, which
        // looks wrong but can't index outside its frames.
        auto not_sprite = [&header, &name](const char* what) {
            LOG_ERROR("texture \"%s\": %s, not treated as a sprite", name.c_str(), what);
            header.isSprite   = false;
            header.spriteAnim = {};
            return header;
        };
        if (image_count > 4096 || (isize)image_count > remain() / 4)
            return not_sprite("wrong image count");

        // bypass image data, store width and height
        std::vector<std::array<float, 2>> imageDatas(image_count, { 0.0f, 0.0f });
        for (usize i_image = 0; i_image < image_count; i_image++) {
            int mipmap_count = file.ReadInt32();
            if (mipmap_count < 0 || mipmap_count > 16) return not_sprite("wrong mipmap count");
            for (int32_t i_mipmap = 0; i_mipmap < mipmap_count; i_mipmap++) {
                if (! SkipMipmapV4Header(file, header)) return not_sprite("file is truncated");
                int32_t width  = file.ReadInt32();
                int32_t height = file.ReadInt32();
                if (i_mipmap == 0) {
                    imageDatas[i_image] = { (float)width, (float)height };
                    header.mipmap_pow2  = algorism::IsPowOfTwo((u32)(width * height));
                }
                if (header.extraHeader["texb"].val > 1) {
                    int32_t LZ4_compressed    = file.ReadInt32();
                    int32_t decompressed_size = file.ReadInt32();
                    (void)LZ4_compressed;
                    (void)decompressed_size;
                }
                i32 src_size = file.ReadInt32();
                if (src_size < 0 || src_size > remain() || ! file.SeekCur(src_size))
                    return not_sprite("file is truncated");
            }
        }
        // sprite pos
        int32_t texs       = ReadTexVesion(file);
        int32_t framecount = file.ReadInt32();
        if (texs > 3) {
            LOG_ERROR("Unkown texs version");
        }
        if (texs == 3) {
            i32 width  = file.ReadInt32();
            i32 height = file.ReadInt32();
            (void)width;
            (void)height;
        }
        // a frame is 32 bytes
        if (framecount <= 0 || framecount > remain() / 32) return not_sprite("wrong frame count");

        for (int32_t i = 0; i < framecount; i++) {
            SpriteFrame sf;
            sf.imageId = file.ReadInt32();
            if (sf.imageId < 0 || (usize)sf.imageId >= image_count)
                return not_sprite("frame uses an image that doesn't exist");
            float spriteWidth  = imageDatas[(usize)sf.imageId][0];
            float spriteHeight = imageDatas[(usize)sf.imageId][1];
            if (! (spriteWidth > 0.0f) || ! (spriteHeight > 0.0f))
                return not_sprite("frame uses an empty image");

            sf.frametime = file.ReadFloat();
            if (texs == 1) {
                sf.x        = (float)file.ReadInt32() / spriteWidth;
                sf.y        = (float)file.ReadInt32() / spriteHeight;
                sf.xAxis[0] = (float)file.ReadInt32();
                sf.xAxis[1] = (float)file.ReadInt32();
                sf.yAxis[0] = (float)file.ReadInt32();
                sf.yAxis[1] = (float)file.ReadInt32();
            } else {
                sf.x        = file.ReadFloat() / spriteWidth;
                sf.y        = file.ReadFloat() / spriteHeight;
                sf.xAxis[0] = file.ReadFloat();
                sf.xAxis[1] = file.ReadFloat();
                sf.yAxis[0] = file.ReadFloat();
                sf.yAxis[1] = file.ReadFloat();
            }
            sf.width  = (float)std::sqrt(std::pow(sf.xAxis[0], 2) + std::pow(sf.xAxis[1], 2));
            sf.height = (float)std::sqrt(std::pow(sf.yAxis[0], 2) + std::pow(sf.yAxis[1], 2));
            // the frame size is used as a divisor later
            if (! (sf.width >= 1.0f) || ! (sf.height >= 1.0f))
                return not_sprite("frame is smaller than a pixel");
            sf.xAxis[0] /= spriteWidth;
            sf.xAxis[1] /= spriteWidth;
            sf.yAxis[0] /= spriteHeight;
            sf.yAxis[1] /= spriteHeight;
            sf.rate = sf.height / sf.width;
            header.spriteAnim.AppendFrame(sf);
        }
    } else {
        i32 mipmap_count = file.ReadInt32();
        (void)mipmap_count;
        if (! SkipMipmapV4Header(file, header)) return header;
        i32 width  = file.ReadInt32();
        i32 height = file.ReadInt32();
        SetHeaderPow2(header, width, height);
    }
    return header;
}
