#include "WPPkgFs.hpp"
#include "Utils/Logging.h"
#include "Fs/LimitedBinaryStream.h"
#include "Fs/CBinaryStream.h"
#include <vector>

using namespace wallpaper;
using namespace wallpaper::fs;

namespace
{
// false when the length doesn't fit in what is left of the file
bool ReadSizedString(IBinaryStream& f, std::string& result) {
    idx ilen = f.ReadInt32();
    if (ilen < 0 || ilen > f.Size() - f.Tell()) return false;

    usize len = (usize)ilen;
    result.resize(len);
    return f.Read(result.data(), len) == len;
}
} // namespace

std::unique_ptr<WPPkgFs> WPPkgFs::CreatePkgFs(std::string_view pkgpath) {
    auto ppkg = fs::CreateCBinaryStream(pkgpath);
    if (! ppkg) return nullptr;

    auto&       pkg = *ppkg;
    const idx   pkgSize = pkg.Size();
    auto        bad     = [&pkgpath]() {
        LOG_ERROR("pkg \"%s\" has a damaged header", std::string(pkgpath).c_str());
        return nullptr;
    };

    std::string ver;
    if (! ReadSizedString(pkg, ver)) return bad();
    LOG_INFO("pkg version: %s", ver.data());

    std::vector<PkgFile> pkgfiles;
    i32                  entryCount = pkg.ReadInt32();
    // an entry is a name length, an offset and a length at least
    if (entryCount < 0 || entryCount > (pkgSize - pkg.Tell()) / 12) return bad();
    for (i32 i = 0; i < entryCount; i++) {
        std::string name;
        if (! ReadSizedString(pkg, name)) return bad();
        idx offset = pkg.ReadInt32();
        idx length = pkg.ReadInt32();
        pkgfiles.push_back({ "/" + name, offset, length });
    }
    auto pkgfs       = std::unique_ptr<WPPkgFs>(new WPPkgFs());
    pkgfs->m_pkgPath = pkgpath;
    idx headerSize   = pkg.Tell();
    for (auto& el : pkgfiles) {
        // entries pointing outside the pkg are dropped, reading them could only fail
        if (el.offset < 0 || el.length < 0 || el.offset > pkgSize - headerSize ||
            el.length > pkgSize - headerSize - el.offset) {
            LOG_ERROR("pkg entry \"%s\" is out of range, ignored", el.path.c_str());
            continue;
        }
        el.offset += headerSize;
        pkgfs->m_files.insert({ el.path, el });
    }
    return pkgfs;
}

bool WPPkgFs::Contains(std::string_view path) const { return m_files.count(std::string(path)) > 0; }

std::shared_ptr<IBinaryStream> WPPkgFs::Open(std::string_view path) {
    auto pkg = fs::CreateCBinaryStream(m_pkgPath);
    if (! pkg) return nullptr;
    if (Contains(path)) {
        const auto& file = m_files.at(std::string(path));
        return std::make_shared<LimitedBinaryStream>(pkg, file.offset, file.length);
    }
    return nullptr;
}

std::shared_ptr<IBinaryStreamW> WPPkgFs::OpenW(std::string_view) { return nullptr; }
