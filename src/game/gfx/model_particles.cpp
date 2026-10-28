// The flexible particle type that draws models (Nx::CModelParticles, the type 0x286A8D26): its constructor and Render
// (see particle_system.h). From the disassembly of 0x0049A0C0-0x0049A4B3 and 0x0049A4C0-0x0049A8DD.

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/model.h"
#include "game/gfx/particle_system.h"
#include "game/gfx/particle_system_internal.h"
#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr unsigned int kMemoryPerModel = 0x1194; // (what's needed free for each model)
constexpr float kScaleStep = 0.01f;             // 0x0064544C
constexpr float kSizeStep = 0.25f;              // 0x006467AC
const char* const kModelExtension = reinterpret_cast<const char*>(0x00646164); // ".mdl"

uint8_t AssertFlag() { return GameVariable<uint8_t>(0x00658555); }
void* Allocate(unsigned int size) { return Mem::Manager::Get()->Allocate(size, AssertFlag(), nullptr); }
void* AllocateZeroed(unsigned int size) { return Mem::Manager::Get()->AllocateZeroed(size, AssertFlag(), nullptr); }
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); } // (the game's C runtime's)
CModel* NewModel(int flag) { return GameFunction<CModel*(__cdecl*)(int)>(0x0048BFA0)(flag); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void ModelFileName(const char* name, const char* ext, char* out)
{
    GameFunction<void(__cdecl*)(const char*, const char*, char*)>(0x00487A70)(name, ext, out);
}

} // namespace

// 0x0049A0C0
CModelParticles* CModelParticles::Construct(uint32_t name, int maxParticles, const char* model, uint32_t, int, int,
                                            float, int)
{
    CParticleSystem::Construct();
    m_name = name;
    char fileName[512] = {};
    mp_positions = nullptr;
    mp_models = nullptr;
    mp_unknown13C = nullptr;
    mp_particles = nullptr;
    m_unknown8C = 0;
    ParticleParts::SetVtables(this, 0x006494A0, 0x0064949C);
    m_maxParticles = maxParticles;
    m_modelCount = static_cast<unsigned int>(maxParticles);
    m_drawnModels = static_cast<unsigned int>(maxParticles);
    mp_particles = ParticleParts::NewParticles(maxParticles);
    if (!mp_particles)
        return this;
    mp_positions = static_cast<float*>(Allocate(m_modelCount * 12));
    if (!mp_positions)
        return this;
    ModelFileName(model, kModelExtension, fileName);
    Str::LowerCase(fileName);
    mp_models = static_cast<void**>(AllocateZeroed(m_modelCount * 4));
    if (!mp_models)
        return this;
    if (Mem::MallocAvailable() < m_modelCount * kMemoryPerModel)
        return this;
    for (unsigned int i = 0; i < m_modelCount; ++i) {
        mp_models[i] = NewModel(AssertFlag());
        auto* each = static_cast<CModel*>(mp_models[i]);
        if (!each)
            return this;
        each->ClearGeoms();
        int percent = Rand() % 100;
        float scale = static_cast<float>(static_cast<double>(percent) * kScaleStep * 2.0 + 1.0);
        Mth::Vector scales = {scale, scale, scale, 1.0f};
        each->SetScale(&scales);
        if (!each->AddFileGeom(fileName, 0, true, 0, false, true, AssertFlag() != 0, true))
            return this;
    }
    auto* rotations = static_cast<Mth::Quat*>(Allocate(m_modelCount * sizeof(Mth::Quat)));
    if (rotations) {
        for (unsigned int i = 0; i < m_modelCount; ++i)
            rotations[i] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the constructor 0x00464340)
    }
    mp_unknown13C = rotations;
    if (!rotations)
        return this;
    mp_startColour = static_cast<uint8_t*>(Allocate(4));
    if (!mp_startColour)
        return this;
    mp_midColour = static_cast<uint8_t*>(Allocate(4));
    if (!mp_midColour)
        return this;
    mp_endColour = static_cast<uint8_t*>(Allocate(4));
    if (!mp_endColour)
        return this;
    for (unsigned int i = 0; i < m_modelCount; ++i) {
        rotations[i].x = static_cast<float>(Rand() % 100);
        rotations[i].y = static_cast<float>(Rand() % 100);
        rotations[i].z = static_cast<float>(Rand() % 100);
        rotations[i].w = static_cast<float>(Rand() % 100);
        static_cast<Mth::Quat*>(mp_unknown13C)->Normalize(1.0f); // (the first, each time: the original's)
    }
    m_colourMid = -1.0f;
    m_flags |= 4u;
    return this;
}

// 0x0049A4C0
void CModelParticles::Render()
{
    auto model = [this](unsigned int i) { return static_cast<CModel*>(mp_models[i]); };
    int count = m_unknown8C;
    if (count == 0) {
        for (unsigned int i = 0; i < m_drawnModels; ++i)
            model(i)->SetActive(0);
        return;
    }
    // (from the last particle's model: it's shown again below)
    for (unsigned int i = static_cast<unsigned int>(count - 1); i < m_drawnModels; ++i)
        model(i)->SetActive(0);
    auto* rotations = static_cast<Mth::Quat*>(mp_unknown13C);
    for (int p = 0; p < count; ++p) {
        sFlexibleParticle& particle = mp_particles[p];
        model(static_cast<unsigned int>(p))->SetActive(1);
        float life = particle.age / particle.lifetime;
        const uint8_t* from;
        const uint8_t* to;
        if (!(m_colourMid >= 0.0f)) { // (no mid colour; NaN too)
            from = mp_startColour;
            to = mp_endColour;
        } else if (life < m_colourMid) {
            life = static_cast<float>(static_cast<double>(life) / m_colourMid);
            from = mp_startColour;
            to = mp_midColour;
        } else {
            life = static_cast<float>((static_cast<double>(life) - m_colourMid) /
                                      (1.0 - static_cast<double>(m_colourMid)));
            from = mp_midColour;
            to = mp_endColour;
        }
        uint32_t fromColour, toColour, colour;
        std::memcpy(&fromColour, from, 4);
        std::memcpy(&toColour, to, 4);
        colour = *LerpColour(&colour, reinterpret_cast<const uint8_t*>(&fromColour),
                             reinterpret_cast<const uint8_t*>(&toColour), life);
        model(static_cast<unsigned int>(p))
            ->SetColor(static_cast<uint8_t>(colour), static_cast<uint8_t>(colour >> 8),
                       static_cast<uint8_t>(colour >> 16), static_cast<uint8_t>(colour >> 24));
        // the rotation moved by the velocity (w by its x too), then normalised
        Mth::Quat& q = rotations[p];
        q.x = static_cast<float>(static_cast<double>(particle.velocity[0]) + q.x);
        q.y = static_cast<float>(static_cast<double>(particle.velocity[1]) + q.y);
        q.z = static_cast<float>(static_cast<double>(particle.velocity[2]) + q.z);
        q.w = static_cast<float>(static_cast<double>(particle.velocity[0]) + q.w);
        double x = q.x, y = q.y, z = q.z, w = q.w;
        double length = std::sqrt(((x * x + y * y) + z * z) + w * w);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            q.x = static_cast<float>(inverse * q.x);
            q.y = static_cast<float>(inverse * q.y);
            q.z = static_cast<float>(inverse * q.z);
            q.w = static_cast<float>(inverse * q.w);
        }
        Mth::Matrix matrix;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                matrix.row[r][c] = r == c ? 1.0f : 0.0f;
        q.GetMatrix(&matrix);
        const float* position = mp_positions + p * 3;
        matrix.row[3] = {position[0], position[1], position[2], 1.0f};
        double shrink = static_cast<double>(p % 4) * kSizeStep;
        double size = static_cast<double>(particle.startSize[0]) - shrink;
        for (int k = 0; k < 3; ++k)
            matrix.row[k][k] = static_cast<float>(size * matrix.row[k][k]);
        model(static_cast<unsigned int>(p))->Render(&matrix, true, nullptr);
    }
}

} // namespace Nx

REPLACE(0x0049A0C0, &Nx::CModelParticles::Construct);
REPLACE(0x0049A4C0, &Nx::CModelParticles::Render);
