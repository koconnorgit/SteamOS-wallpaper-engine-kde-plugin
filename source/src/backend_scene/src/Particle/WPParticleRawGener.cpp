#include "WPParticleRawGener.h"

#include <algorithm>
#include <cstring>
#include <Eigen/Dense>
#include <array>

#include "Core/Literals.hpp"
#include "SpecTexs.hpp"
#include "ParticleModify.h"
#include "ParticleSystem.h"

#include "Utils/Logging.h"

using namespace wallpaper;
using namespace Eigen;

struct WPGOption {
    bool thick_format { false };
    bool geometry_shader { false };
};

namespace
{
inline void AssignVertexTimes(std::span<float> dst, std::span<const float> src, uint num) noexcept {
    const uint dst_one_size = dst.size() / num;
    for (uint i = 0; i < num; i++) {
        std::copy(src.begin(), src.end(), dst.begin() + i * dst_one_size);
    }
}

inline void AssignVertex(std::span<float> dst, std::span<const float> src, uint num) noexcept {
    const uint dst_one_size = dst.size() / num;
    const uint src_one_size = src.size() / num;
    for (uint i = 0; i < num; i++) {
        std::copy_n(src.begin() + i * src_one_size, src_one_size, dst.begin() + i * dst_one_size);
    }
}

inline usize GenParticleData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                             const ParticleRawGenSpecOp& specOp, WPGOption opt,
                             SceneVertexArray& sv) noexcept {
    std::array<float, 32 * 4> storage;

    float* data = storage.data();

    const auto one_size   = sv.OneSize();
    const auto totle_size = 4 * one_size;
    usize      i { 0 };
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;

        for (const auto& p : inst->Particles()) {
            if (! ParticleModify::LifetimeOk(p)) {
                continue;
            }

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });

            auto  pos  = inst->GetBoundedData().pos + p.position;
            float size = p.size / 2.0f;

            usize offset = 0;

            // pos
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { pos[0], pos[1], pos[2] }, 4);
            offset += 4;
            // TexCoordVec4
            float      rz = p.rotation[2];
            std::array t { 0.0f, 1.0f, rz, size, 1.0f, 1.0f, rz, size,
                           1.0f, 0.0f, rz, size, 0.0f, 0.0f, rz, size };
            AssignVertex({ data + offset, totle_size }, t, 4);
            offset += 4;

            // color
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { p.color[0], p.color[1], p.color[2], p.alpha },
                              4);
            offset += 4;

            if (opt.thick_format) {
                AssignVertexTimes(
                    { data + offset, totle_size },
                    std::array { p.velocity[0], p.velocity[1], p.velocity[2], lifetime },
                    4);
                offset += 4;
            }
            // TexCoordC2
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { p.rotation[0], p.rotation[1] }, 4);

            sv.SetVertexs((i++) * 4, { data, totle_size });
        }
    }
    return i;
}

// Indices are 16 bit and a particle is four vertices, so a mesh can address
// this many particles and no more.
constexpr size_t MAX_PARTICLES_PER_MESH = 65536 / 4;

inline void updateIndexArray(size_t index, size_t count, SceneIndexArray& iarray) noexcept {
    constexpr size_t single_size = 6;
    count                        = std::min(count, MAX_PARTICLES_PER_MESH);
    if (index >= count) return;
    const uint16_t cv = (uint16_t)(index * 4);

    std::array<uint16_t, single_size> single;
    // 0 1 3
    // 1 2 3
    single[0] = cv;
    single[1] = cv + 1;
    single[2] = cv + 3;
    single[3] = cv + 1;
    single[4] = cv + 2;
    single[5] = cv + 3;
    // every particle
    for (size_t i = index; i < count; i++) {
        iarray.AssignHalf(i * single_size, single);
        for (auto& x : single) x += 4;
    }
}
} // namespace

void WPParticleRawGener::GenGLData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                   SceneMesh& mesh, ParticleRawGenSpecOp& specOp) {
    auto& sv = mesh.GetVertexArray(0);
    auto& si = mesh.GetIndexArray(0);

    WPGOption opt;

    opt.thick_format = sv.GetOption(WE_CB_THICK_FORMAT);

    usize particle_num { 0 };

    particle_num += GenParticleData(instances, specOp, opt, sv);

    // LOG_INFO("num: %d", particle_num);

    particle_num = std::min(particle_num, MAX_PARTICLES_PER_MESH);

    usize indexNum = (si.DataCount() * 2) / 6;
    if (particle_num > indexNum) {
        updateIndexArray(indexNum, particle_num, si);
    }
    si.SetRenderDataCount(particle_num * 6 / 2);
}
