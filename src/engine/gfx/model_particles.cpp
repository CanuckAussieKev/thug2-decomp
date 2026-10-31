// The flexible particle type that draws models (Engine3D::CModelParticles, the type 0x286A8D26): its constructor and Render
// (see particle_system.h). From the disassembly of 0x0049A0C0-0x0049A4B3 and 0x0049A4C0-0x0049A8DD.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_system_internal.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/core/game_vtables.h"
#include "platform/crt.h"

namespace Engine3D {

namespace {

constexpr unsigned int kMemoryPerModel = 0x1194; // (what's needed free for each model)
constexpr float kScaleStep = 0.01f;             // 0x0064544C
constexpr float kSizeStep = 0.25f;              // 0x006467AC
const char* const kModelExtension = ".mdl"; // ".mdl"

uint8_t AssertFlag() { return g_allocAssertFlag; }
void* Carve(unsigned int size) { return MemSys::Hub::Pull()->Carve(size, AssertFlag(), nullptr); }
void* AllocateZeroed(unsigned int size) { return MemSys::Hub::Pull()->AllocateZeroed(size, AssertFlag(), nullptr); }
int Rand() { return Crt::Rand(); } // (the game's C runtime's)
CRenderModel* SpawnRenderModel(int) { return static_cast<CRenderModel*>(Engine3D::SpawnModel()); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void ModelPathFor(const char* name, const char* ext, char* out)
{
    Gfx2D::MakeModelFileName(name, ext, out);
}

} // namespace

// 0x0049A0C0
CModelParticles* CModelParticles::MakeNew(uint32_t name, int particleCap, const char* model, uint32_t, int, int,
                                            float, int)
{
    CEmitterInstance::MakeNew();
    m_label = name;
    char fileName[512] = {};
    m_positionArray = nullptr;
    m_modelList = nullptr;
    m_unknown13CPtr = nullptr;
    m_particleList = nullptr;
    m_unknown8C = 0;
    ParticleParts::SetVtables(this, Vt::kModelParticles, Vt::kModelParticlesNode);
    m_maxParticles = particleCap;
    m_modelCount = static_cast<unsigned int>(particleCap);
    m_drawnModels = static_cast<unsigned int>(particleCap);
    m_particleList = ParticleParts::NewParticles(particleCap);
    if (!m_particleList)
        return this;
    m_positionArray = static_cast<float*>(Carve(m_modelCount * 12));
    if (!m_positionArray)
        return this;
    ModelPathFor(model, kModelExtension, fileName);
    TextUtil::ToLowerText(fileName);
    m_modelList = static_cast<void**>(AllocateZeroed(m_modelCount * 4));
    if (!m_modelList)
        return this;
    if (MemSys::MallocAvailable() < m_modelCount * kMemoryPerModel)
        return this;
    for (unsigned int i = 0; i < m_modelCount; ++i) {
        m_modelList[i] = SpawnRenderModel(AssertFlag());
        auto* each = static_cast<CRenderModel*>(m_modelList[i]);
        if (!each)
            return this;
        each->DropGeoms();
        int percent = Rand() % 100;
        float scale = static_cast<float>(static_cast<double>(percent) * kScaleStep * 2.0 + 1.0);
        Maths::Vec4 scales = {scale, scale, scale, 1.0f};
        each->ChangeScale(&scales);
        if (!each->AddFileGeom(fileName, 0, true, 0, false, true, AssertFlag() != 0, true))
            return this;
    }
    auto* rotations = static_cast<Maths::QuatRotation*>(Carve(m_modelCount * sizeof(Maths::QuatRotation)));
    if (rotations) {
        for (unsigned int i = 0; i < m_modelCount; ++i)
            rotations[i] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the constructor 0x00464340)
    }
    m_unknown13CPtr = rotations;
    if (!rotations)
        return this;
    m_startColour = static_cast<uint8_t*>(Carve(4));
    if (!m_startColour)
        return this;
    m_midColour = static_cast<uint8_t*>(Carve(4));
    if (!m_midColour)
        return this;
    m_endColour = static_cast<uint8_t*>(Carve(4));
    if (!m_endColour)
        return this;
    for (unsigned int i = 0; i < m_modelCount; ++i) {
        rotations[i].x = static_cast<float>(Rand() % 100);
        rotations[i].y = static_cast<float>(Rand() % 100);
        rotations[i].z = static_cast<float>(Rand() % 100);
        rotations[i].w = static_cast<float>(Rand() % 100);
        static_cast<Maths::QuatRotation*>(m_unknown13CPtr)->MakeUnit(1.0f); // (the first, each time: the original's)
    }
    m_colourMid = -1.0f;
    m_flagBits |= 4u;
    return this;
}

// 0x0049A4C0
void CModelParticles::Render()
{
    auto model = [this](unsigned int i) { return static_cast<CRenderModel*>(m_modelList[i]); };
    int count = m_unknown8C;
    if (count == 0) {
        for (unsigned int i = 0; i < m_drawnModels; ++i)
            model(i)->ChangeLive(0);
        return;
    }
    // (from the last particle's model: it's shown again below)
    for (unsigned int i = static_cast<unsigned int>(count - 1); i < m_drawnModels; ++i)
        model(i)->ChangeLive(0);
    auto* rotations = static_cast<Maths::QuatRotation*>(m_unknown13CPtr);
    for (int p = 0; p < count; ++p) {
        sFlexibleParticle& particle = m_particleList[p];
        model(static_cast<unsigned int>(p))->ChangeLive(1);
        float life = particle.age / particle.lifetime;
        const uint8_t* from;
        const uint8_t* to;
        if (!(m_colourMid >= 0.0f)) { // (no mid colour; NaN too)
            from = m_startColour;
            to = m_endColour;
        } else if (life < m_colourMid) {
            life = static_cast<float>(static_cast<double>(life) / m_colourMid);
            from = m_startColour;
            to = m_midColour;
        } else {
            life = static_cast<float>((static_cast<double>(life) - m_colourMid) /
                                      (1.0 - static_cast<double>(m_colourMid)));
            from = m_midColour;
            to = m_endColour;
        }
        uint32_t fromColour, toColour, colour;
        std::memcpy(&fromColour, from, 4);
        std::memcpy(&toColour, to, 4);
        colour = *LerpColour(&colour, reinterpret_cast<const uint8_t*>(&fromColour),
                             reinterpret_cast<const uint8_t*>(&toColour), life);
        model(static_cast<unsigned int>(p))
            ->ChangeTint(static_cast<uint8_t>(colour), static_cast<uint8_t>(colour >> 8),
                       static_cast<uint8_t>(colour >> 16), static_cast<uint8_t>(colour >> 24));
        // the rotation moved by the velocity (w by its x too), then normalised
        Maths::QuatRotation& q = rotations[p];
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
        Maths::Mat4 matrix;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                matrix.row[r][c] = r == c ? 1.0f : 0.0f;
        q.FetchMat(&matrix);
        const float* position = m_positionArray + p * 3;
        matrix.row[3] = {position[0], position[1], position[2], 1.0f};
        double shrink = static_cast<double>(p % 4) * kSizeStep;
        double size = static_cast<double>(particle.startSize[0]) - shrink;
        for (int k = 0; k < 3; ++k)
            matrix.row[k][k] = static_cast<float>(size * matrix.row[k][k]);
        model(static_cast<unsigned int>(p))->Render(&matrix, true, nullptr);
    }
}

} // namespace Nx
