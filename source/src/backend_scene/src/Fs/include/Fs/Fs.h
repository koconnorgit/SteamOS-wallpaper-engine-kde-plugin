#pragma once
#include <memory>

#include "IBinaryStream.h"
#include "Core/NoCopyMove.hpp"

namespace wallpaper
{
namespace fs
{

class Fs : NoCopy,NoMove {
public:
	virtual bool Contains(std::string_view path) const = 0;
	virtual std::shared_ptr<IBinaryStream> Open(std::string_view path) = 0;
	virtual std::shared_ptr<IBinaryStreamW> OpenW(std::string_view path) = 0;
	// replaces `to` in one step, false when the fs is read only or it failed
	virtual bool Rename(std::string_view, std::string_view) { return false; }
public:
	Fs() = default;
	virtual ~Fs() = default;
};

}
}
