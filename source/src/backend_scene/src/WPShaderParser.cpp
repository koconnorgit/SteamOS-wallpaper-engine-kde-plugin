#include "WPShaderParser.hpp"

#include "Fs/IBinaryStream.h"
#include "Utils/Logging.h"
#include "Utils/LoadStats.hpp"
#include "WPJson.hpp"

#include "wpscene/WPUniform.h"
#include "Fs/VFS.h"
#include "Utils/Sha.hpp"
#include "Utils/String.h"
#include "WPCommon.hpp"
#include "SpecTexs.hpp"

#include "Vulkan/ShaderComp.hpp"

#include <regex>
#include <stack>
#include <charconv>
#include <string>
#include <limits>
#include <thread>
#include <functional>
#include <mutex>
#include <unordered_map>

static constexpr std::string_view SHADER_PLACEHOLD { "__SHADER_PLACEHOLD__" };

#define SHADER_DIR    "spvs01"
#define SHADER_SUFFIX "spvs"
// file format version, bump when the layout written by SaveShaderToFile changes
#define SHADER_CACHE_VERSION 1
// part of the cache key, bump when the compile options below change
#define SHADER_COMPILE_VERSION "c1"

using namespace wallpaper;

namespace
{

static constexpr const char* pre_shader_code = R"(#version 150
#define GLSL 1
#define HLSL 0
#define highp

#define CAST2(x) (vec2(x))
#define CAST3(x) (vec3(x))
#define CAST4(x) (vec4(x))
#define CAST3X3(x) (mat3(x))

#define texSample2D texture
#define texSample2DLod textureLod
#define mul(x, y) ((y) * (x))
#define frac fract
#define atan2 atan
#define fmod(x, y) (x-y*trunc(x/y))
#define ddx dFdx
#define ddy(x) dFdy(-(x))
#define saturate(x) (clamp(x, 0.0, 1.0))

#define max(x, y) max(y, x)

#define float1 float
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define lerp mix

__SHADER_PLACEHOLD__

)";

static constexpr const char* pre_shader_code_vert = R"(
#define attribute in
#define varying out

)";
static constexpr const char* pre_shader_code_frag = R"(
#define varying in
#define gl_FragColor glOutColor
out vec4 glOutColor;

)";

inline std::string LoadGlslInclude(fs::VFS& vfs, const std::string& input, int depth = 0) {
    std::string::size_type pos = 0;
    std::string            output;
    std::string::size_type linePos = std::string::npos;

    // a header that includes itself would never end
    if (depth > 16) {
        LOG_ERROR("shader includes are nested too deep, not expanding further");
        return {};
    }

    while (linePos = input.find("#include", pos), linePos != std::string::npos) {
        auto lineEnd = input.find_first_of('\n', linePos);
        // an include on the last line, without a newline
        if (lineEnd == std::string::npos) lineEnd = input.size();
        auto lineSize = lineEnd - linePos;
        auto lineStr  = input.substr(linePos, lineSize);
        output.append(input.substr(pos, linePos - pos));

        auto inP         = lineStr.find_first_of('\"') + 1;
        auto inE         = lineStr.find_last_of('\"');
        auto includeName = lineStr.substr(inP, inE - inP);
        auto includeSrc  = fs::GetFileContent(vfs, "/assets/shaders/" + includeName);
        output.append("\n//-----include " + includeName + "\n");
        output.append(LoadGlslInclude(vfs, includeSrc, depth + 1));
        output.append("\n//-----include end\n");

        pos = lineEnd;
    }
    output.append(input.substr(pos));
    return output;
}

inline void ParseWPShader(const std::string& src, WPShaderInfo* pWPShaderInfo,
                          const std::vector<WPShaderTexInfo>& texinfos) {
    auto& combos       = pWPShaderInfo->combos;
    auto& wpAliasDict  = pWPShaderInfo->alias;
    auto& shadervalues = pWPShaderInfo->svs;
    auto& defTexs      = pWPShaderInfo->defTexs;
    idx   texcount     = std::ssize(texinfos);

    // pos start of line
    std::string::size_type pos = 0, lineEnd = std::string::npos;
    while ((lineEnd = src.find_first_of(('\n'), pos)), true) {
        const auto clineEnd = lineEnd;
        const auto line     = src.substr(pos, lineEnd - pos);

        /*
        if(line.find("attribute ") != std::string::npos || line.find("in ") != std::string::npos) {
            update_pos = true;
        }
        */
        if (line.find("// [COMBO]") != std::string::npos) {
            nlohmann::json combo_json;
            auto brace = line.find_first_of('{');
            if (brace != std::string::npos && PARSE_JSON(line.substr(brace), combo_json)) {
                if (combo_json.contains("combo")) {
                    std::string name;
                    int32_t     value = 0;
                    GET_JSON_NAME_VALUE(combo_json, "combo", name);
                    GET_JSON_NAME_VALUE(combo_json, "default", value);
                    combos[name] = std::to_string(value);
                }
            }
        } else if (line.find("uniform ") != std::string::npos) {
            if (line.find("// {") != std::string::npos) {
                nlohmann::json sv_json;
                if (PARSE_JSON(line.substr(line.find_first_of('{')), sv_json)) {
                    std::vector<std::string> defines =
                        utils::SpliteString(line.substr(0, line.find_first_of(';')), ' ');

                    std::string material;
                    GET_JSON_NAME_VALUE_NOWARN(sv_json, "material", material);
                    if (! material.empty()) wpAliasDict[material] = defines.back();

                    ShaderValue sv;
                    std::string name  = defines.back();
                    bool        istex = name.compare(0, 9, "g_Texture") == 0;
                    if (istex) {
                        wpscene::WPUniformTex wput;
                        wput.FromJson(sv_json);
                        i32 index { 0 };
                        STRTONUM(name.substr(9), index);
                        if (index < 0 || index >= (i32)WE_GLTEX_NAMES.size()) {
                            // can't be bound; with nothing left in wput the code
                            // below does nothing for it
                            LOG_ERROR("shader texture \"%s\" is not a valid slot", name.c_str());
                            wput  = {};
                            index = std::numeric_limits<i32>::max();
                        }
                        if (! wput.default_.empty()) defTexs.push_back({ index, wput.default_ });
                        if (! wput.combo.empty()) {
                            if (index >= texcount)
                                combos[wput.combo] = "0";
                            else
                                combos[wput.combo] = "1";
                        }
                        if (index < texcount && texinfos[(usize)index].enabled) {
                            auto& compos = texinfos[(usize)index].composEnabled;

                            usize num = std::min(std::size(compos), std::size(wput.components));
                            for (usize i = 0; i < num; i++) {
                                if (compos[i]) combos[wput.components[i].combo] = "1";
                            }
                        }

                    } else {
                        if (sv_json.contains("default")) {
                            auto        value = sv_json.at("default");
                            ShaderValue sv;
                            name = defines.back();
                            if (value.is_string()) {
                                std::vector<float> v;
                                GET_JSON_VALUE(value, v);
                                sv = std::span<const float>(v);
                            } else if (value.is_number()) {
                                sv.setSize(1);
                                GET_JSON_VALUE(value, sv[0]);
                            }
                            shadervalues[name] = sv;
                        }
                        if (sv_json.contains("combo")) {
                            std::string name;
                            GET_JSON_NAME_VALUE(sv_json, "combo", name);
                            combos[name] = "1";
                        }
                    }
                    if (defines.back()[0] != 'g') {
                        LOG_INFO("PreShaderSrc User shadervalue not supported");
                    }
                }
            }
        }

        // end
        if (line.find("void main()") != std::string::npos || clineEnd == std::string::npos) {
            break;
        }
        pos = lineEnd + 1;
    }
}

inline usize FindIncludeInsertPos(const std::string& src, usize startPos) {
    /* rule:
    after attribute/varying/uniform/struct
    befor any func
    not in {}
    not in #if #endif
    */
    (void)startPos;

    auto NposToZero = [](usize p) {
        return p == std::string::npos ? 0 : p;
    };
    auto search = [](const std::string& p, usize pos, const auto& re) {
        auto        startpos = p.begin() + (isize)pos;
        std::smatch match;
        if (startpos < p.end() && std::regex_search(startpos, p.end(), match, re)) {
            return pos + (usize)match.position();
        }
        return std::string::npos;
    };
    auto searchLast = [](const std::string& p, const auto& re) {
        auto        startpos = p.begin();
        std::smatch match;
        while (startpos < p.end() && std::regex_search(startpos, p.end(), match, re)) {
            startpos++;
            startpos += match.position();
        }
        return startpos >= p.end() ? std::string::npos : usize(startpos - p.begin());
    };
    auto nextLinePos = [](const std::string& p, usize pos) {
        return p.find_first_of('\n', pos) + 1;
    };

    usize mainPos  = src.find("void main(");
    bool  two_main = src.find("void main(", mainPos + 2) != std::string::npos;
    if (two_main) return 0;

    usize pos;
    {
        static const std::regex reAfters(R"(\n(attribute|varying|uniform|struct) )");
        usize            afterPos = searchLast(src, reAfters);
        if (afterPos != std::string::npos) {
            afterPos = nextLinePos(src, afterPos + 1);
        }
        pos = std::min({ NposToZero(afterPos), mainPos });
    }
    {
        std::stack<usize> ifStack;
        usize             nowPos { 0 };
        static const std::regex reIfs(R"((#if|#endif))");
        while (true) {
            auto p = search(src, nowPos + 1, reIfs);
            if (p > mainPos || p == std::string::npos) break;
            if (src.substr(p, 3) == "#if") {
                ifStack.push(p);
            } else {
                if (ifStack.empty()) break;
                usize ifp = ifStack.top();
                ifStack.pop();
                usize endp = p;
                if (pos > ifp && pos <= endp) {
                    pos = nextLinePos(src, endp + 1);
                }
            }
            nowPos = p;
        }
        pos = std::min({ pos, mainPos });
    }

    return NposToZero(pos);
}

inline EShLanguage ToGLSL(ShaderType type) {
    switch (type) {
    case ShaderType::VERTEX: return EShLangVertex;
    case ShaderType::FRAGMENT: return EShLangFragment;
    default: return EShLangVertex;
    }
}

inline std::string Preprocessor(const std::string& in_src, ShaderType type, const Combos& combos,
                                WPPreprocessorInfo& process_info) {
    std::string res;

    std::string src = wallpaper::WPShaderParser::PreShaderHeader(in_src, combos, type);

    // workaround #require directive
    {
        static const std::regex re_require("(^|\r?\n)#require (.+)(\r?\n)");
        src = std::regex_replace(src, re_require, "$1//#require $2$3");
    }

    glslang::TShader::ForbidIncluder includer;
    glslang::TShader                 shader(ToGLSL(type));
    const EShMessages emsg { (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgRelaxedErrors |
                                           EShMsgSuppressWarnings | EShMsgVulkanRules) };

    auto* data = src.c_str();
    shader.setStrings(&data, 1);
    shader.preprocess(&vulkan::DefaultTBuiltInResource,
                      110,
                      EProfile::ECoreProfile,
                      false,
                      false,
                      emsg,
                      &res,
                      includer);

    static const std::regex re_io(R"(.+\s(in|out)\s[\s\w]+\s(\w+)\s*;)", std::regex::ECMAScript);
    for (auto it = std::sregex_iterator(res.begin(), res.end(), re_io);
         it != std::sregex_iterator();
         it++) {
        std::smatch mc = *it;
        if (mc[1] == "in") {
            process_info.input[mc[2]] = mc[0].str();
        } else {
            process_info.output[mc[2]] = mc[0].str();
        }
    }

    static const std::regex re_tex(R"(uniform\s+sampler2D\s+g_Texture(\d+))", std::regex::ECMAScript);
    for (auto it = std::sregex_iterator(res.begin(), res.end(), re_tex);
         it != std::sregex_iterator();
         it++) {
        std::smatch mc  = *it;
        auto        str = mc[1].str();
        uint        slot;
        auto [ptr, ec] { std::from_chars(str.c_str(), str.c_str() + str.size(), slot) };
        if (ec != std::errc()) continue;
        process_info.active_tex_slots.insert(slot);
    }
    return res;
}

inline std::string Finalprocessor(const WPShaderUnit& unit, const WPPreprocessorInfo* pre,
                                  const WPPreprocessorInfo* next) {
    std::string insert_str {};
    auto&       cur = unit.preprocess_info;
    if (pre != nullptr) {
        for (auto& [k, v] : pre->output) {
            if (! exists(cur.input, k)) {
                static const std::regex re_out(R"(\s*out\s)");
                auto                    n = std::regex_replace(v, re_out, " in ");
                insert_str += n + '\n';
            }
        }
    }
    if (next != nullptr) {
        for (auto& [k, v] : next->input) {
            if (! exists(cur.output, k)) {
                static const std::regex re_in(R"(\s*in\s)");
                auto                    n = std::regex_replace(v, re_in, " out ");
                insert_str += n + '\n';
            }
        }
    }
    // the placeholder is a fixed string, no need for a regex
    std::string out = unit.src;
    for (auto pos = out.find(SHADER_PLACEHOLD); pos != std::string::npos;
         pos      = out.find(SHADER_PLACEHOLD, pos + insert_str.size())) {
        out.replace(pos, SHADER_PLACEHOLD.size(), insert_str);
    }
    return out;
}

// Some workshop shaders declare a varying with different vector sizes in
// the vertex and fragment stage (hlsl accepts that, glsl fails at link).
// Make the fragment input match the vertex output, and feed the original
// fragment code a converted copy.
inline void FixVaryingMismatch(const std::string& vert_src, std::string& frag_src) {
    auto components = [](const std::string& type) -> usize {
        if (type == "float") return 1;
        return (usize)(type.back() - '0');
    };
    auto scan = [](const std::string& src, const char* io) {
        Map<std::string, std::string> res; // name to type
        const std::regex re(std::string(R"(\b)") + io + R"(\s+(?:\w+\s+)*?(float|vec[234])\s+(\w+)\s*;)");
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re);
             it != std::sregex_iterator();
             it++) {
            res[(*it)[2].str()] = (*it)[1].str();
        }
        return res;
    };

    const auto vert_outs = scan(vert_src, "out");
    const auto frag_ins  = scan(frag_src, "in");

    std::string prologue;
    for (const auto& [name, frag_type] : frag_ins) {
        if (! exists(vert_outs, name)) continue;
        const auto& vert_type = vert_outs.at(name);
        if (vert_type == frag_type) continue;

        const std::string local = "_wek_" + name;

        // the fragment code uses the local copy from now on
        frag_src = std::regex_replace(frag_src, std::regex("\\b" + name + "\\b"), local);
        // and its declaration becomes the real input plus the local copy
        const std::regex re_decl(R"(\bin(\s+(?:\w+\s+)*?))" + frag_type + R"(\s+)" + local +
                                 R"(\s*;)");
        frag_src = std::regex_replace(frag_src,
                                      re_decl,
                                      "in$1" + vert_type + " " + name + ";\n" + frag_type + " " +
                                          local + ";",
                                      std::regex_constants::format_first_only);

        const usize nv = components(vert_type), nf = components(frag_type);
        std::string value = name;
        for (usize i = nv; i < nf; i++) value += ", 0.0";
        prologue += "    " + local + " = " + frag_type + "(" + value + ");\n";

        LOG_INFO("varying \"%s\" is %s in vertex but %s in fragment, converted",
                 name.c_str(),
                 vert_type.c_str(),
                 frag_type.c_str());
    }
    if (prologue.empty()) return;

    const std::regex re_main(R"(\bvoid\s+main\s*\()");
    frag_src = std::regex_replace(frag_src, re_main, "void _wek_main(");
    frag_src += "\nvoid main() {\n" + prologue + "    _wek_main();\n}\n";
}

// Compiled shaders by the hash of what went in, shared by every scene in the
// process. Entries are small (the spir-v of a material's stages).
class ShaderMemo {
public:
    static bool Find(const std::string& key, std::span<WPShaderUnit> units,
                     std::vector<ShaderCode>& codes) {
        std::lock_guard<std::mutex> lock(mutex());
        auto                        it = entries().find(key);
        if (it == entries().end() || it->second.infos.size() != units.size()) return false;
        codes = it->second.codes;
        for (usize i = 0; i < units.size(); i++) units[i].preprocess_info = it->second.infos[i];
        return true;
    }
    static void Store(const std::string& key, std::span<const WPShaderUnit> units,
                      const std::vector<ShaderCode>& codes) {
        std::lock_guard<std::mutex> lock(mutex());
        // a wallpaper has a few hundred at most; this only bounds a very long session
        if (entries().size() >= 4096) entries().clear();
        Entry e;
        e.codes = codes;
        for (const auto& unit : units) e.infos.push_back(unit.preprocess_info);
        entries()[key] = std::move(e);
    }

private:
    struct Entry {
        std::vector<ShaderCode>         codes;
        std::vector<WPPreprocessorInfo> infos;
    };
    static std::mutex& mutex() {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<std::string, Entry>& entries() {
        static std::unordered_map<std::string, Entry> e;
        return e;
    }
};

inline std::string GenSha1(std::span<const WPShaderUnit> units) {
    std::string shas { SHADER_COMPILE_VERSION };
    for (auto& unit : units) {
        shas += utils::genSha1(unit.src);
    }
    return utils::genSha1(shas);
}
inline std::string GetCachePath(std::string_view scene_id, std::string_view filename) {
    return std::string("/cache/") + std::string(scene_id) + "/" SHADER_DIR "/" +
           std::string(filename) + "." SHADER_SUFFIX;
}

// A cache file can be empty, truncated or from another version. Anything that
// doesn't check out is treated as a miss, and the caller compiles again.
inline bool LoadShaderFromFile(std::vector<ShaderCode>& codes, fs::IBinaryStream& file,
                               usize expected_count) {
    constexpr u32 spirv_magic { 0x07230203 };

    codes.clear();
    if (ReadSPVVesion(file) != SHADER_CACHE_VERSION) return false;

    usize count = file.ReadUint32();
    if (count == 0 || count != expected_count) return false;

    codes.resize(count);
    for (usize i = 0; i < count; i++) {
        auto& c = codes[i];

        u32   size   = file.ReadUint32();
        isize remain = file.Size() - file.Tell();
        if (size < 4 || size % 4 != 0 || (isize)size > remain) return false;

        c.resize(size / 4);
        if (file.Read((char*)c.data(), size) != size) return false;
        if (c[0] != spirv_magic) return false;
    }
    return true;
}

inline void SaveShaderToFile(std::span<const ShaderCode> codes, fs::IBinaryStreamW& file) {
    char nop[256] { '\0' };

    WriteSPVVesion(file, SHADER_CACHE_VERSION);
    file.WriteUint32((u32)codes.size());
    for (const auto& c : codes) {
        u32 size = (u32)c.size() * 4;
        file.WriteUint32(size);
        file.Write((const char*)c.data(), size);
    }
    file.Write(nop, sizeof(nop));
}

} // namespace

std::string WPShaderParser::PreShaderSrc(fs::VFS& vfs, const std::string& src,
                                         WPShaderInfo*                       pWPShaderInfo,
                                         const std::vector<WPShaderTexInfo>& texinfos) {
    std::string            newsrc(src);
    std::string::size_type pos = 0;
    std::string            include;
    while (pos = src.find("#include", pos), pos != std::string::npos) {
        auto begin = pos;
        pos        = src.find_first_of('\n', pos);
        if (pos == std::string::npos) pos = src.size();
        newsrc.replace(begin, pos - begin, pos - begin, ' ');
        include.append(src.substr(begin, pos - begin) + "\n");
    }
    include = LoadGlslInclude(vfs, include);

    ParseWPShader(include, pWPShaderInfo, texinfos);
    ParseWPShader(newsrc, pWPShaderInfo, texinfos);

    newsrc.insert(FindIncludeInsertPos(newsrc, 0), include);
    return newsrc;
}

std::string WPShaderParser::PreShaderHeader(const std::string& src, const Combos& combos,
                                            ShaderType type) {
    std::string pre(pre_shader_code);
    if (type == ShaderType::VERTEX) pre += pre_shader_code_vert;
    if (type == ShaderType::FRAGMENT) pre += pre_shader_code_frag;
    std::string header(pre);
    for (const auto& c : combos) {
        std::string cup(c.first);
        std::transform(c.first.begin(), c.first.end(), cup.begin(), ::toupper);
        if (c.second.empty()) {
            LOG_ERROR("combo '%s' can't be empty", cup.c_str());
            continue;
        }
        std::string value(c.second);
        // effects bundled in the pkg (glitter, caustics) default to blend mode 32,
        // which common_blending.h doesn't know and would treat as normal (replace).
        // They are meant to add light, so use linear add.
        if (cup == "BLENDMODE" && value == "32") value = "31";
        header.append("#define " + cup + " " + value + "\n");
    }
    return header + src;
}

void WPShaderParser::InitGlslang() { glslang::InitializeProcess(); }
void WPShaderParser::FinalGlslang() { glslang::FinalizeProcess(); }

bool WPShaderParser::CompileToSpv(std::string_view scene_id, std::span<WPShaderUnit> units,
                                  std::vector<ShaderCode>& codes, fs::VFS& vfs,
                                  WPShaderInfo* shader_info, std::span<const WPShaderTexInfo> texs) {
    (void)texs;
    LoadStats::Timer timer(LoadStats::get().shader_ms);

    // Most of a scene's materials use the same few shaders with the same
    // combos, and every monitor showing a scene asks for the same ones again.
    // The result of everything below is remembered for the life of the process.
    std::string memo_key;
    {
        std::string all { SHADER_COMPILE_VERSION };
        for (const auto& c : shader_info->combos) all += c.first + "=" + c.second + ";";
        for (const auto& unit : units) {
            all += "\n@stage " + std::to_string((int)unit.stage) + "\n";
            all += unit.src;
        }
        memo_key = utils::genSha1(all);
    }
    if (ShaderMemo::Find(memo_key, units, codes)) return true;
    auto memo = [&memo_key, &units, &codes](bool ok) {
        if (ok) ShaderMemo::Store(memo_key, units, codes);
        return ok;
    };

    std::for_each(units.begin(), units.end(), [shader_info](auto& unit) {
        unit.src = Preprocessor(unit.src, unit.stage, shader_info->combos, unit.preprocess_info);
    });

    // The sources are final before the cache key is taken, so a change to any
    // step up to here gives a new key by itself.
    for (usize i = 0; i < units.size(); i++) {
        auto&               unit     = units[i];
        WPPreprocessorInfo* pre_info = i >= 1 ? &units[i - 1].preprocess_info : nullptr;
        WPPreprocessorInfo* post_info =
            i + 1 < units.size() ? &units[i + 1].preprocess_info : nullptr;

        unit.src = Finalprocessor(unit, pre_info, post_info);

        if (i >= 1 && unit.stage == ShaderType::FRAGMENT &&
            units[i - 1].stage == ShaderType::VERTEX) {
            FixVaryingMismatch(units[i - 1].src, unit.src);
        }
    }

    auto compile = [](std::span<WPShaderUnit> units, std::vector<ShaderCode>& codes) {
        std::vector<vulkan::ShaderCompUnit> vunits(units.size());
        for (usize i = 0; i < units.size(); i++) {
            vunits[i].src   = units[i].src;
            vunits[i].stage = ToGLSL(units[i].stage);
        }

        vulkan::ShaderCompOpt opt;
        opt.client_ver             = glslang::EShTargetVulkan_1_1;
        opt.auto_map_bindings      = true;
        opt.auto_map_locations     = true;
        opt.relaxed_errors_glsl    = true;
        opt.relaxed_rules_vulkan   = true;
        opt.suppress_warnings_glsl = true;

        std::vector<vulkan::Uni_ShaderSpv> spvs(units.size());

        if (! vulkan::CompileAndLinkShaderUnits(vunits, opt, spvs)) {
            return false;
        }

        codes.clear();
        for (auto& spv : spvs) {
            codes.emplace_back(std::move(spv->spirv));
        }
        return true;
    };

    bool has_cache_dir = vfs.IsMounted("cache");

    if (has_cache_dir) {
        std::string sha1            = GenSha1(units);
        std::string cache_file_path = GetCachePath(scene_id, sha1);

        if (vfs.Contains(cache_file_path)) {
            auto cache_file = vfs.Open(cache_file_path);
            if (cache_file && ::LoadShaderFromFile(codes, *cache_file, units.size()))
                return memo(true);
            LOG_ERROR("shader cache \'%s\' is not usable, compiling again",
                      cache_file_path.c_str());
        }

        if (! compile(units, codes)) return false;

        // Written under another name and renamed, so a reader (another monitor
        // loading the same scene) never sees a partial file.
        std::string tmp_path =
            cache_file_path + "." +
            std::to_string(std::hash<std::thread::id> {}(std::this_thread::get_id())) + ".tmp";
        bool written { false };
        if (auto cache_file = vfs.OpenW(tmp_path); cache_file) {
            ::SaveShaderToFile(codes, *cache_file);
            written = true;
        }
        if (written && ! vfs.Rename(tmp_path, cache_file_path)) {
            LOG_ERROR("can't save shader cache \'%s\'", cache_file_path.c_str());
        }
        return memo(true);

    } else {
        return memo(compile(units, codes));
    }
}
