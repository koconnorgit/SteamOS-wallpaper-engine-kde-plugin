#pragma once
#include <cstdint>

namespace wallpaper
{

// A video that fills a texture, e.g. an mp4 stored in a scene as a ".tex".
// It plays on its own clock; the renderer asks for the picture once a frame.
class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    // Writes the current picture as width x height pixels of 4 bytes, red,
    // green, blue and one byte of no meaning (rows top to bottom, no padding)
    // if it changed since the last call that returned true. Returns false,
    // leaving the buffer alone, when it didn't. The buffer may be write-only
    // memory: it is not read back.
    virtual bool NextFrame(uint8_t* rgba) = 0;

    virtual void SetPaused(bool paused) = 0;
};

} // namespace wallpaper
