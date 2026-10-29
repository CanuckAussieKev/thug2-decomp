// An animation's frames (see boned_anim_frame_data.h): from the disassembly of 0x00484EB0-0x004859E8.

#include "game/gfx/boned_anim_frame_data.h"

#include <cstring>

#include "game/gfx/anim_blend.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/mth/x87_math.h"
#include "game/obj/walk_hang.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

constexpr uint32_t kTwiceTranslation = 0x4000;
constexpr uint32_t kCacheOnLoad = 0x800;
constexpr uint32_t kNoAutoCache = 0x1000;
constexpr uint32_t kTranslationKeys = 0x20000;
constexpr uint32_t kBoneBits = 0x80000;
constexpr uint32_t kShortCounts = 0x400000;
constexpr uint32_t kCompressed = 0x800000;
constexpr uint32_t kOtherInterpolation = 0x9000000;
constexpr uint32_t kCompressedB = 0x10000000;
constexpr float kKeyScale = 1.0f / 32.0f; // 0x006487CC: a compressed translation's units
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0
constexpr int kCachesPerFrame = 10;

// The animation cache (the manager 0x006F92B0 through its getter) and its entries' calls (none rewritten yet).
void* CacheManager() { return GameFunction<void*(__cdecl*)()>(0x00480660)(); }
int CacheFormat(void* manager) { return GameFunction<int(__thiscall*)(void*)>(0x00480800)(manager); }
template <typename R, typename... A>
R CacheCall(uint32_t address, void* object, A... args)
{
    return GameFunction<R(__thiscall*)(void*, A...)>(address)(object, args...);
}

// The compressed keys' decoders: the next rotation key (0x00482C20) / translation key (0x00482E70) into `key`,
// returning where the next one starts; a rotation key as a quaternion (0x00482A70, 0x00483D00 - the quaternion at +4
// of its output).
uint8_t* DecodeRotation(uint8_t* cursor, void* key, uint32_t flags, uint8_t* table)
{
    return GameFunction<uint8_t*(__cdecl*)(uint8_t*, void*, uint32_t, uint8_t*)>(0x00482C20)(cursor, key, flags, table);
}
uint8_t* DecodeTranslation(uint8_t* cursor, void* key, uint32_t flags)
{
    return GameFunction<uint8_t*(__cdecl*)(uint8_t*, void*, uint32_t)>(0x00482E70)(cursor, key, flags);
}
void RotationToQuatA(void* out, const void* key) { GameFunction<void(__cdecl*)(void*, const void*)>(0x00482A70)(out, key); }
void RotationToQuatB(void* out, const void* key) { GameFunction<void(__cdecl*)(void*, const void*)>(0x00483D00)(out, key); }

// The vblank count when caches were last built (0x006FA2E0) and how many have been since (0x006FA2D8).
uint64_t& LastCacheVblank() { return GameVariable<uint64_t>(0x006FA2E0); }
uint32_t& CachesBuilt() { return GameVariable<uint32_t>(0x006FA2D8); }

// A plain key's time: its first short (rotation keys: 15 bits, signed).
int RotationKeyTime(const uint8_t* key)
{
    return static_cast<int16_t>(static_cast<int16_t>(*reinterpret_cast<const uint16_t*>(key) << 1) >> 1);
}
int TranslationKeyTime(const uint8_t* key) { return *reinterpret_cast<const int16_t*>(key); }

// The pair of keys of `count` around `time` (each key 8 bytes) and how far between: the last key - itself twice, 0.
// Without one, `from` / `to` / `fraction` stay as they were.
void FindKeys(uint8_t* keys, int count, float time, int (*keyTime)(const uint8_t*), uint8_t*& from, uint8_t*& to,
              float& fraction)
{
    for (int i = 0; i < count; ++i) {
        uint8_t* key = keys + i * 8;
        if (i == count - 1) {
            from = key;
            to = key;
            fraction = 0.0f;
            return;
        }
        double t0 = keyTime(key);
        if (!(t0 <= time))
            continue;
        double t1 = keyTime(key + 8);
        if (!(t1 >= time))
            continue;
        from = key;
        to = key + 8;
        fraction = static_cast<float>(X87::Divide(time - t0, t1 - t0));
        return;
    }
}

} // namespace

// 0x00484EB0
bool CBonedAnimFrameData::ApplyAdditive(Mth::Quat* rotations, Mth::Vector* translations, void*, float weight,
                                        uint8_t* mask)
{
    uint8_t* rotKeys = mp_quatKeys;
    uint8_t* transKeys = mp_transKeys;
    uint16_t* rotSizes = mp_quatSizes;
    uint16_t* transSizes = mp_transSizes;
    uint32_t maskBit = mask && mask[0x208] ? 1u << (*reinterpret_cast<uint32_t*>(mask + 0x210) & 31) : 0u;
    for (int i = 0; i < m_boneCount; ++i) {
        bool skip = maskBit ? ((*reinterpret_cast<uint32_t**>(mask + 0x20C))[i] & maskBit) != 0 : false;
        if (m_flags & kBoneBits)
            skip = !(mp_partialBones[1 + i / 32] & (1u << (i % 32)));
        if (!skip) {
            Mth::Quat& rotation = rotations[i];
            if (*rotSizes) {
                uint8_t key[8];
                uint8_t decoded[20];
                DecodeRotation(rotKeys, key, m_flags, mp_keyTable);
                RotationToQuatB(decoded, key);
                Mth::Quat added = rotation;
                added *= *reinterpret_cast<Mth::Quat*>(decoded + 4);
                Mth::Quat to = added;
                float blended[4];
                float* result = InterpolateRotation(blended, &rotation.x, &to.x, weight);
                rotation = {result[0], result[1], result[2], result[3]};
            }
            uint32_t flags = m_flags;
            if (!(flags & kTranslationKeys) || *transSizes) {
                Mth::Vector& translation = translations[i];
                uint8_t key[8];
                DecodeTranslation(transKeys, key, flags);
                const auto* values = reinterpret_cast<const int16_t*>(key + 2);
                double x = values[0] * static_cast<double>(kKeyScale);
                float y = static_cast<float>(values[1] * static_cast<double>(kKeyScale));
                float z = static_cast<float>(values[2] * static_cast<double>(kKeyScale));
                Mth::Vector target;
                if ((m_flags & kTwiceTranslation) && (m_flags & kTranslationKeys)) {
                    float zSum = static_cast<float>(static_cast<double>(z) + translation.z);
                    float wSum = static_cast<float>(static_cast<double>(translation.w) + 1.0f);
                    target.x = static_cast<float>((x + translation.x) + translation.x);
                    target.y = static_cast<float>((static_cast<double>(y) + translation.y) + translation.y);
                    target.z = static_cast<float>(static_cast<double>(zSum) + translation.z);
                    target.w = static_cast<float>(static_cast<double>(wSum) + translation.w);
                } else {
                    target.x = static_cast<float>(x + translation.x);
                    target.y = static_cast<float>(static_cast<double>(y) + translation.y);
                    target.z = static_cast<float>(static_cast<double>(z) + translation.z);
                    target.w = static_cast<float>(static_cast<double>(translation.w) + 1.0f);
                }
                Mth::Vector moved;
                translation = *Walk::Lerp(&moved, &translation, &target, weight);
            }
        }
        rotKeys += *rotSizes++;
        transKeys += *transSizes++;
    }
    return true;
}

// 0x00485250
bool CBonedAnimFrameData::ApplyAdditiveIfCompressed(Mth::Quat* rotations, Mth::Vector* translations, void* unused,
                                                    float weight, uint8_t* mask)
{
    if (!(m_flags & kCompressed))
        return false;
    return ApplyAdditive(rotations, translations, unused, weight, mask);
}

// 0x00485270
void CBonedAnimFrameData::BuildCache(int format)
{
    if (CachesBuilt() >= kCachesPerFrame) {
        if (Tmr::GetVblanks() == LastCacheVblank())
            return;
        CachesBuilt() = 0;
        LastCacheVblank() = Tmr::GetVblanks();
    } else if (Tmr::GetVblanks() != LastCacheVblank()) {
        CachesBuilt() = 0;
        LastCacheVblank() = Tmr::GetVblanks();
    }
    void* manager = CacheManager();
    uint32_t size = CacheCall<uint32_t>(0x00480670, manager, m_boneCount, static_cast<int>(m_quatKeyCount),
                                        static_cast<int>(m_transKeyCount), format);
    int priority = static_cast<int16_t>(static_cast<int16_t>(m_bits << 7) >> 8);
    if (*reinterpret_cast<int*>(static_cast<uint8_t*>(manager) + 0xC8) == 0x32 ||
        CacheCall<uint32_t>(0x00480940, manager) < size) {
        if (CacheCall<bool>(0x00480ED0, manager, priority, size))
            mp_cacheEntry = nullptr;
    }
    uint8_t* cache = CacheCall<uint8_t*>(0x00480980, manager, this, m_boneCount, static_cast<int>(m_quatKeyCount),
                                         static_cast<int>(m_transKeyCount), format);
    if (!cache)
        return;
    auto* rotOffsets = CacheCall<uint16_t*>(0x00480620, cache);
    auto* transOffsets = CacheCall<uint16_t*>(0x00480630, cache);
    uint32_t rotSize = CacheCall<uint8_t>(0x004805E0, cache);
    uint32_t transSize = CacheCall<uint8_t>(0x004805F0, cache);
    int bones = m_boneCount;
    uint8_t* rotData = CacheCall<uint8_t*>(0x00480600, cache, 0);
    uint8_t* transData = CacheCall<uint8_t*>(0x00480610, cache, 0);
    // (one buffer for a rotation key and a converted translation: the latter's bytes 2-3 are the former's)
    alignas(4) uint8_t key[16];
    alignas(4) uint8_t quatA[16];
    alignas(4) uint8_t quatB[20];
    alignas(4) uint8_t translation[8];

    // The rotations: each bone's keys one after another, the bones' starts in rotOffsets
    uint16_t* sizes = mp_quatSizes;
    uint8_t* cursor = mp_quatKeys;
    uint8_t* end = cursor + *sizes;
    uint32_t offset = 0;
    uint32_t bone = 0;
    rotOffsets[0] = 0;
    transOffsets[0] = 0;
    for (;;) {
        if (*sizes == 0) {
            do {
                end = cursor + sizes[1];
                ++sizes;
                ++bone;
                rotOffsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
            } while (static_cast<uint16_t>(bone) != static_cast<uint32_t>(bones) && *sizes == 0);
            if (static_cast<uint16_t>(bone) == static_cast<uint32_t>(bones))
                break;
        }
        cursor = DecodeRotation(cursor, key, m_flags, mp_keyTable);
        const uint8_t* source = key;
        if (format == 3) {
            RotationToQuatA(quatA, key);
            source = quatA;
        } else if (format == 4) {
            RotationToQuatB(quatB, key);
            source = quatB;
        }
        std::memcpy(rotData + static_cast<uint16_t>(offset), source, rotSize);
        offset += rotSize;
        if (cursor != end)
            continue;
        end = cursor + sizes[1];
        ++sizes;
        ++bone;
        rotOffsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == static_cast<uint32_t>(bones))
            break;
    }

    // The translations
    sizes = mp_transSizes;
    cursor = mp_transKeys;
    end = cursor + *sizes;
    offset = 0;
    bone = 0;
    for (;;) {
        if (*sizes == 0) {
            do {
                end = cursor + sizes[1];
                ++sizes;
                ++bone;
                transOffsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
            } while (static_cast<uint16_t>(bone) != static_cast<uint32_t>(bones) && *sizes == 0);
            if (static_cast<uint16_t>(bone) == static_cast<uint32_t>(bones))
                break;
        }
        cursor = DecodeTranslation(cursor, translation, m_flags);
        const uint8_t* source = translation;
        if (format >= 3 && format <= 4) {
            const auto* values = reinterpret_cast<const int16_t*>(translation + 2);
            std::memcpy(key, translation, 2);
            for (int i = 0; i < 3; ++i) {
                float value = static_cast<float>(values[i] * static_cast<double>(kKeyScale));
                std::memcpy(key + 4 + i * 4, &value, 4);
            }
            source = key;
        }
        std::memcpy(transData + static_cast<uint16_t>(offset), source, transSize);
        offset += transSize;
        if (cursor != end)
            continue;
        end = cursor + sizes[1];
        ++sizes;
        ++bone;
        transOffsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == static_cast<uint32_t>(bones))
            break;
    }

    mp_cacheEntry = cache;
    CacheCall<void>(0x00480640, cache, 0x00482B90u, this); // (the entry's owner and its release callback)
    uint16_t& cacheFlags = *reinterpret_cast<uint16_t*>(mp_cacheEntry + 0x164);
    cacheFlags = static_cast<uint16_t>(((m_flags >> 11) & 1) << 13 | (cacheFlags & 0xDFFF));
    ++CachesBuilt();
    LastCacheVblank() = Tmr::GetVblanks();
}

// 0x004856C0
bool CBonedAnimFrameData::Load(const uint8_t* data, uint32_t, bool)
{
    auto* header = reinterpret_cast<const uint32_t*>(data);
    m_unknown00 = header[2];
    m_flags = header[1];
    bool loaded = false;
    const uint8_t* keys = data + 0xC;
    if (header[1] & kCompressedB)
        loaded = GameFunction<bool(__thiscall*)(CBonedAnimFrameData*, const uint8_t*)>(0x00484370)(this, keys);
    else if (header[1] & kCompressed)
        loaded = GameFunction<bool(__thiscall*)(CBonedAnimFrameData*, const uint8_t*)>(0x00484200)(this, keys);
    if (m_flags & kCacheOnLoad)
        BuildCache(CacheFormat(CacheManager()));
    return loaded;
}

// 0x00485730
bool CBonedAnimFrameData::GetFrames(Mth::Quat* rotations, Mth::Vector* translations, uint8_t* arg2, float time,
                                    int arg4, void* arg5)
{
    uint32_t flags = m_flags;
    if (flags & kCompressed) {
        if (!(flags & kNoAutoCache) && !mp_cacheEntry)
            BuildCache(CacheFormat(CacheManager()));
        return GameFunction<bool(__thiscall*)(CBonedAnimFrameData*, Mth::Quat*, Mth::Vector*, uint8_t*, float, int,
                                              void*)>(0x00484760)(this, rotations, translations, arg2, time, arg4, arg5);
    }
    float frame = time * kFramesPerSecond;
    uint8_t* rotKeys = mp_quatKeys;
    uint8_t* transKeys = mp_transKeys;
    uint8_t* rotFrom = rotKeys;
    uint8_t* rotTo = rotKeys;
    uint8_t* transFrom = transKeys;
    uint8_t* transTo = transKeys;
    float rotFraction = 0.0f;
    float transFraction = 0.0f;
    for (int i = 0; i < m_boneCount; ++i) {
        int rotCount;
        int transCount;
        if (m_flags & kShortCounts) {
            rotCount = reinterpret_cast<int16_t*>(mp_keyCounts)[i * 2];
            transCount = reinterpret_cast<int16_t*>(mp_keyCounts)[i * 2 + 1];
        } else {
            rotCount = mp_keyCounts[i * 2];
            transCount = mp_keyCounts[i * 2 + 1];
        }
        FindKeys(rotKeys, rotCount, frame, RotationKeyTime, rotFrom, rotTo, rotFraction);
        GameFunction<void(__cdecl*)(Mth::Quat*, uint8_t*, uint8_t*, float, int)>(0x00483B30)(
            rotations, rotFrom, rotTo, rotFraction, (m_flags & kOtherInterpolation) ? 1 : 0);
        FindKeys(transKeys, transCount, frame, TranslationKeyTime, transFrom, transTo, transFraction);
        GameFunction<void(__cdecl*)(Mth::Vector*, uint8_t*, uint8_t*, float, int)>(0x00483C40)(
            translations, transFrom, transTo, transFraction, (m_flags & kOtherInterpolation) ? 1 : 0);
        rotKeys += rotCount * 8;
        rotations++;
        transKeys += transCount * 8;
        translations++;
    }
    return true;
}

} // namespace Gfx

REPLACE(0x00484EB0, &Gfx::CBonedAnimFrameData::ApplyAdditive);
REPLACE(0x00485250, &Gfx::CBonedAnimFrameData::ApplyAdditiveIfCompressed);
REPLACE(0x00485270, &Gfx::CBonedAnimFrameData::BuildCache);
REPLACE(0x004856C0, &Gfx::CBonedAnimFrameData::Load);
REPLACE(0x00485730, &Gfx::CBonedAnimFrameData::GetFrames);
