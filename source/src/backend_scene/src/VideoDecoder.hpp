#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "Interface/IVideoSource.h"

namespace wallpaper
{

// Plays a video held in memory and hands out its frames scaled to
// width x height. Returns nullptr when the build has no video support or the
// player can't be started.
std::shared_ptr<IVideoSource> CreateVideoDecoder(std::vector<uint8_t> data, int32_t width,
                                                 int32_t height);

} // namespace wallpaper
