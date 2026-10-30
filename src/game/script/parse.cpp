#include "game/script/parse.h"

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/mth/random.h"
#include "game/script/eval.h"
#include "game/script/qb.h"
#include "game/script/cscript.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/movie.h"

namespace Script {

GAME_GLOBAL(float, g_parseNudge, 0x00645D74, 0.1f);
GAME_GLOBAL(float, g_parseRandomScale, 0x0064840C, 0.000030525032f);
GAME_GLOBAL(CStoredRandom*, g_storedRandomsNewest, 0x006F2358);
GAME_GLOBAL(CStoredRandom*, g_storedRandomsOldest, 0x006F235C);
GAME_GLOBAL(int, g_storedRandomCount, 0x006F2360);
GAME_GLOBAL(CExpressionEvaluator, g_evaluator, 0x006F2370);
GAME_GLOBAL(CComponent, g_parseScratch, 0x006F2568);

namespace {

float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void SetType(CComponent* component, uint32_t type)
{
    component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (type << 1));
}

const uint8_t* SkipEndOfLinesIn(const uint8_t* p)
{
    for (;;) {
        if (*p == 0x01)
            p += 1;
        else if (*p == 0x02)
            p += 5;
        else
            return p;
    }
}

CStruct* NewInitStruct()
{
    CStruct* structure = NewStruct();
    return structure ? structure->Init() : nullptr;
}
CArray* NewInitArray()
{
    CArray* array = NewArray();
    return array ? array->Init() : nullptr;
}

// The evaluator expressions are parsed with, and the component values go through on the way.
CExpressionEvaluator& Evaluator() { return g_evaluator; }
CComponent& Scratch() { return g_parseScratch; }

// The stored randoms: newest first (next = older), at most 100.
CStoredRandom*& StoredRandomsNewest() { return g_storedRandomsNewest; }
CStoredRandom*& StoredRandomsOldest() { return g_storedRandomsOldest; }
int& StoredRandomCount() { return g_storedRandomCount; }

// An expression parameter of a structure / call: merged if it gives a structure, added (unnamed) otherwise.
const uint8_t* AddExpression(const uint8_t* token, CStruct* dest, CStruct* args)
{
    CComponent* value = NewComponent();
    token = ParseExpression(token, args, value);
    if (value->Type() == ESYMBOLTYPE_STRUCTURE) {
        dest->AppendStructureIfAny(value->structure);
        CleanUpComponent(value);
        FreeComponent(value);
    } else if (value->Type() != ESYMBOLTYPE_NONE) {
        value->name = 0;
        dest->AddComponent(value);
    } else {
        FreeComponent(value);
    }
    return token;
}

// A `name = (expression)` parameter.
const uint8_t* AddNamedExpression(const uint8_t* token, uint32_t name, CStruct* dest, CStruct* args)
{
    CComponent* value = NewComponent();
    token = ParseExpression(token, args, value);
    if (value->typeAndFlag & 0xFE) {
        value->name = name;
        dest->AddComponent(value);
    } else {
        FreeComponent(value);
    }
    return token;
}

} // namespace

// 0x004728C0
CScript* GetCurrentScript() { return g_currentScript; }

// 0x004728D0
CScript* GetNextScript(CScript* script)
{
    return script ? *reinterpret_cast<CScript**>(script) : g_scriptListHead;
}

// 0x0046F7A0 (the table in EBX, the type in EDI, the count in ESI)
CStoredRandom* FindStoredRandom(const uint8_t* table, uint32_t type, uint32_t count)
{
    uint32_t script = reinterpret_cast<uint32_t>(GetCurrentScript());
    for (CStoredRandom* stored = StoredRandomsNewest(); stored; stored = stored->m_next) {
        if (stored->m_table == table && stored->m_type == type && stored->m_count == count &&
            (stored->m_owner == script || stored->m_owner == 0))
            return stored;
    }
    return nullptr;
}

// 0x00470BB0
CStoredRandom* NewStoredRandom()
{
    if (StoredRandomCount() >= 100) {
        CStoredRandom* oldest = StoredRandomsOldest();
        CStoredRandom* newer = oldest->m_previous;
        --StoredRandomCount();
        FreeStoredRandom(oldest);
        StoredRandomsOldest() = newer;
        if (newer)
            newer->m_next = nullptr;
    }
    auto* stored = static_cast<CStoredRandom*>(g_storedRandomPools.Allocate(sizeof(CStoredRandom)));
    if (stored) {
        ++StoredRandomCount();
        stored->m_next = nullptr;
        stored->m_previous = nullptr;
    }
    CStoredRandom* newest = StoredRandomsNewest();
    stored->m_previous = nullptr;
    stored->m_next = newest;
    StoredRandomsNewest() = stored;
    if (newest)
        newest->m_previous = stored;
    else
        StoredRandomsOldest() = stored;
    return stored;
}

// 0x00470C60
const uint8_t* ResolveRandomJumps(const uint8_t* token)
{
    for (;;) {
        switch (*token) {
        case 0x2E: // jump
            token += 5 + Read4Bytes(token + 1);
            break;
        case 0x2F:   // Random(@a @b ...): weighted
        case 0x37: { // the same, from the second random stream
            uint32_t count = Read4Bytes(token + 1);
            const uint8_t* weights = token + 5;
            int total = 0;
            for (uint32_t i = 0; i < count; ++i)
                total += static_cast<int16_t>(Read2Bytes(weights + 2 * i));
            int pick = *token == 0x2F ? Mth::Rnd(total) : Mth::Rnd2(total);
            int choice = 0;
            pick -= static_cast<int16_t>(Read2Bytes(weights));
            while (pick >= 0) {
                ++choice;
                pick -= static_cast<int16_t>(Read2Bytes(weights + 2 * choice));
            }
            const uint8_t* offset = weights + 2 * count + 4 * choice;
            token = offset + Read4Bytes(offset) + 4;
            break;
        }
        case 0x40: { // RandomNoRepeat
            uint32_t count = Read4Bytes(token + 1);
            const uint8_t* table = token + 5 + 2 * count;
            CStoredRandom* stored = FindStoredRandom(table, 0x40, count);
            if (!stored) {
                stored = NewStoredRandom();
                stored->m_table = table;
                stored->m_type = 0x40;
                stored->m_count = static_cast<uint16_t>(count);
                stored->m_last = static_cast<uint32_t>(Mth::Rnd(static_cast<int>(count)));
            }
            stored->m_owner = reinterpret_cast<uint32_t>(GetCurrentScript());
            int pick = Mth::Rnd(static_cast<int>(count));
            for (int tries = 0; static_cast<uint16_t>(pick) == stored->m_last;) {
                if (++tries > 100)
                    break;
                pick = Mth::Rnd(static_cast<int>(count));
            }
            uint32_t choice = static_cast<uint16_t>(pick);
            stored->m_last = choice;
            const uint8_t* offset = table + 4 * choice;
            token = offset + Read4Bytes(offset) + 4;
            break;
        }
        case 0x41: { // RandomPermute
            uint32_t count = Read4Bytes(token + 1);
            const uint8_t* table = token + 5 + 2 * count;
            CStoredRandom* stored = FindStoredRandom(table, 0x41, count);
            if (!stored) {
                stored = NewStoredRandom();
                stored->m_table = table;
                stored->m_type = 0x41;
                stored->m_count = static_cast<uint16_t>(count);
                stored->m_index = 0;
                for (uint16_t i = 0; i < stored->m_count; ++i)
                    stored->m_order[i] = static_cast<uint8_t>(i);
                stored->Shuffle(false);
            }
            stored->m_owner = reinterpret_cast<uint32_t>(GetCurrentScript());
            if (++stored->m_index >= stored->m_count) {
                stored->Shuffle(true);
                stored->m_index = 0;
            }
            const uint8_t* offset = table + 4 * stored->m_order[stored->m_index];
            token = offset + Read4Bytes(offset) + 4;
            break;
        }
        case 0x49: // short jump
            token += 1 + Read2Bytes(token + 1);
            break;
        default:
            return token;
        }
    }
}

// 0x0046FD60 (the token in EAX)
const uint8_t* SkipValue(const uint8_t* token)
{
    switch (*token) {
    case 0x03: case 0x05: {
        uint8_t open = *token, close = static_cast<uint8_t>(open + 1);
        int depth = 1;
        do {
            token = SkipToken(token);
            if (*token == open)
                ++depth;
            else if (*token == close)
                --depth;
        } while (depth);
        return token + 1;
    }
    case 0x16: case 0x17: case 0x18: case 0x1A:
        return token + 5;
    case 0x1B: case 0x1C:
        return token + 5 + Read4Bytes(token + 1);
    case 0x1E:
        return token + 13;
    case 0x1F:
        return token + 9;
    default:
        return token;
    }
}

// 0x0046FBD0 (the token in EAX, `deterministic` in BL)
const uint8_t* ParseRandomRange(const uint8_t* token, CComponent* out, bool deterministic)
{
    float a = AsFloat(Read4Bytes(token + 1));
    float b = AsFloat(Read4Bytes(token + 5));
    // (rounded with the game's 0.1 nudge, as the x87 does it)
    const double nudge = g_parseNudge;
    const double tolerance = Gfx::g_tinyLength;
    auto nearest = [&](float f) {
        return static_cast<int>(f >= 0.0f || std::isnan(f) ? static_cast<double>(f) + nudge
                                                           : static_cast<double>(f) - nudge);
    };
    int low = nearest(a);
    int high = nearest(b);
    if (!(std::fabs(static_cast<double>(a) - low) > tolerance) &&
        !(std::fabs(static_cast<double>(b) - high) > tolerance)) {
        // Whole numbers: an integer in [low, high].
        if (low > high) {
            int swapped = low;
            low = high;
            high = swapped;
        }
        int range = high - low + 1;
        int pick = deterministic ? Mth::Rnd2(range) : Mth::Rnd(range);
        out->raw = static_cast<uint32_t>(pick + low);
        SetType(out, ESYMBOLTYPE_INTEGER);
        return token + 9;
    }
    if (a > b) {
        float swapped = a;
        a = b;
        b = swapped;
    }
    int pick = deterministic ? Mth::Rnd2(0x7FF9) : Mth::Rnd(0x7FF9);
    SetType(out, ESYMBOLTYPE_FLOAT);
    double scale = g_parseRandomScale;
    out->floatValue = static_cast<float>((static_cast<double>(b) - a) * pick * scale + a);
    return token + 9;
}

// 0x00479730
void ResolveNameComponent(CComponent* component)
{
    if ((component->typeAndFlag & 0xFE) != (ESYMBOLTYPE_NAME << 1))
        return;
    CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
    if (!symbol)
        return;
    switch (symbol->type) {
    case ESYMBOLTYPE_INTEGER: case ESYMBOLTYPE_FLOAT: case ESYMBOLTYPE_STRING: case ESYMBOLTYPE_LOCALSTRING:
    case ESYMBOLTYPE_PAIR: case ESYMBOLTYPE_VECTOR: case ESYMBOLTYPE_STRUCTURE: case ESYMBOLTYPE_ARRAY:
    case ESYMBOLTYPE_NAME:
        SetType(component, symbol->type);
        component->raw = symbol->raw;
        break;
    default:
        break;
    }
}

// 0x004706F0
const uint8_t* ParseValue(const uint8_t* token, CStruct* args, CComponent* out)
{
    const uint8_t* p = token + 1;
    switch (*token) {
    case 0x03: {
        CStruct* structure = NewInitStruct();
        p = ParseStructure(structure, token, args);
        SetType(out, ESYMBOLTYPE_STRUCTURE);
        out->structure = structure;
        return p;
    }
    case 0x05: {
        Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
        CArray* array = NewInitArray();
        Mem::PopContext();
        p = ParseArray(array, token, args);
        SetType(out, ESYMBOLTYPE_ARRAY);
        out->array = array;
        return p;
    }
    case 0x16:
        SetType(out, ESYMBOLTYPE_NAME);
        out->raw = Read4Bytes(p);
        return token + 5;
    case 0x17:
        SetType(out, ESYMBOLTYPE_INTEGER);
        out->raw = Read4Bytes(p);
        return token + 5;
    case 0x1A:
        SetType(out, ESYMBOLTYPE_FLOAT);
        out->raw = Read4Bytes(p);
        return token + 5;
    case 0x1B:
    case 0x1C: {
        SetType(out, *token == 0x1B ? ESYMBOLTYPE_STRING : ESYMBOLTYPE_LOCALSTRING);
        uint32_t length = Read4Bytes(p);
        out->string = CreateString(reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x1E: {
        SetType(out, ESYMBOLTYPE_VECTOR);
        uint32_t x = Read4Bytes(p), y = Read4Bytes(token + 5), z = Read4Bytes(token + 9);
        CVector* vector = NewVector();
        vector = vector ? vector->Init() : nullptr;
        out->vector = vector;
        std::memcpy(&vector->x, &x, 4);
        std::memcpy(&out->vector->y, &y, 4);
        std::memcpy(&out->vector->z, &z, 4);
        return token + 13;
    }
    case 0x1F: {
        SetType(out, ESYMBOLTYPE_PAIR);
        uint32_t x = Read4Bytes(p), y = Read4Bytes(token + 5);
        CPair* pair = NewPair();
        pair = pair ? pair->Init() : nullptr;
        out->pair = pair;
        std::memcpy(&pair->x, &x, 4);
        std::memcpy(&out->pair->y, &y, 4);
        return token + 9;
    }
    case 0x23: { // a script: its name goes in the component's name
        out->name = Read4Bytes(token + 2);
        const uint8_t* start = token + 6;
        const uint8_t* end = SkipToEndOfScript(start);
        SetType(out, ESYMBOLTYPE_QSCRIPT);
        uint32_t size = static_cast<uint32_t>(end - start);
        Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
        auto* code = static_cast<uint8_t*>(Mem::Malloc(size));
        Mem::PopContext();
        for (uint32_t i = 0; i < size; ++i)
            code[i] = start[i];
        out->raw = reinterpret_cast<uint32_t>(code);
        return end;
    }
    case 0x2C: // <...>: all the caller's parameters
        if (args) {
            CStruct* structure = NewInitStruct();
            structure->Assign(args);
            SetType(out, ESYMBOLTYPE_STRUCTURE);
            out->structure = structure;
        }
        return token + 1;
    case 0x2D: { // <name>: one of the caller's parameters
        uint32_t name = Read4Bytes(token + 2);
        if (args) {
            if (CComponent* found = args->FindLastComponent(name))
                CopyComponent(out, found, 0);
        }
        return token + 6;
    }
    case 0x30:
        return ParseRandomRange(p, out, false);
    case 0x38:
        return ParseRandomRange(p, out, true);
    default:
        return token + 1;
    }
}

// 0x00470130 (the token in ECX, the name in EDX)
const uint8_t* AddParameter(const uint8_t* token, uint32_t name, CStruct* dest, CStruct* args)
{
    const uint8_t* p = token + 1;
    bool deterministic = false;
    switch (*token) {
    case 0x16:
        dest->AddChecksum(name, Read4Bytes(p));
        return token + 5;
    case 0x17:
        dest->AddInteger(name, static_cast<int>(Read4Bytes(p)));
        return token + 5;
    case 0x1A:
        dest->AddFloat(name, AsFloat(Read4Bytes(p)));
        return token + 5;
    case 0x1E: {
        float x = AsFloat(Read4Bytes(p)), y = AsFloat(Read4Bytes(token + 5)), z = AsFloat(Read4Bytes(token + 9));
        // (a zero vector named 0x9D2D0915 isn't added)
        const float tolerance = Nx::g_onePercent;
        if (name != 0x9D2D0915 || !(std::fabs(x) < tolerance) || !(std::fabs(y) < tolerance) ||
            !(std::fabs(z) < tolerance))
            dest->AddVector(name, x, y, z);
        return token + 13;
    }
    case 0x1F:
        dest->AddPair(name, AsFloat(Read4Bytes(p)), AsFloat(Read4Bytes(token + 5)));
        return token + 9;
    case 0x1B: {
        uint32_t length = Read4Bytes(p);
        dest->AddString(name, reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x1C: {
        uint32_t length = Read4Bytes(p);
        dest->AddLocalString(name, reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x03: {
        CStruct* structure = NewInitStruct();
        p = ParseStructure(structure, token, args);
        dest->AddStructurePointer(name, structure);
        return p;
    }
    case 0x05: {
        Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
        CArray* array = NewInitArray();
        Mem::PopContext();
        p = ParseArray(array, token, args);
        dest->AddArrayPointer(name, array);
        return p;
    }
    case 0x23: { // a script, under its own name
        uint32_t scriptName = Read4Bytes(token + 2);
        const uint8_t* start = token + 6;
        const uint8_t* end = SkipToEndOfScript(start);
        dest->AddScript(scriptName, start, static_cast<uint32_t>(end - start));
        return end;
    }
    case 0x2D: { // <name>
        uint32_t reference = Read4Bytes(token + 2);
        p = token + 6;
        if (!args)
            return p;
        CComponent* found = args->FindLastComponent(reference);
        if (!found)
            return p;
        if (name == 0 && (found->typeAndFlag & 0xFE) == (ESYMBOLTYPE_STRUCTURE << 1)) {
            dest->AppendStructureIfAny(found->structure);
            return p;
        }
        CComponent* copy = NewComponent();
        CopyComponent(copy, found, 0);
        copy->name = name;
        dest->AddComponent(copy);
        return p;
    }
    case 0x2C: // <...>
        if (args) {
            if (name) {
                CStruct* structure = NewInitStruct();
                structure->Assign(args);
                dest->AddStructurePointer(name, structure);
            } else {
                dest->AppendStructure(args);
            }
        }
        return p;
    case 0x38:
        deterministic = true;
        [[fallthrough]];
    case 0x30: {
        CComponent* value = NewComponent();
        p = ParseRandomRange(p, value, deterministic);
        value->name = name;
        dest->AddComponent(value);
        return p;
    }
    case 0x09:
        return p;
    default:
        return token;
    }
}

// 0x00470F30
const uint8_t* ParseArguments(CStruct* dest, const uint8_t* token, CStruct* args)
{
    for (;;) {
        token = ResolveRandomJumps(token);
        uint8_t t = *token;
        if (t == 0x01 || t == 0x02 || t == 0x00 || t == 0x0F)
            return token;
        switch (t) {
        case 0x03:
            token = ParseStructure(dest, token, args);
            break;
        case 0x09:
            ++token;
            break;
        case 0x2C:
            ++token;
            if (args)
                dest->AppendStructure(args);
            break;
        case 0x2D:
            token = AddParameter(token, 0, dest, args);
            break;
        case 0x16: {
            uint32_t name = Read4Bytes(token + 1);
            const uint8_t* next = ResolveRandomJumps(token + 5);
            if (*next != 0x07) {
                // A bare name: a flag.
                token = AddParameter(token, 0, dest, args);
                break;
            }
            next = ResolveRandomJumps(next + 1);
            if (*next == 0x0E)
                token = AddNamedExpression(next, name, dest, args);
            else
                token = AddParameter(next, name, dest, args);
            break;
        }
        default:
            if (t == 0x0E)
                token = AddExpression(token, dest, args);
            else
                token = AddParameter(token, 0, dest, args);
            break;
        }
    }
}

// 0x00471860
const uint8_t* ParseStructure(CStruct* dest, const uint8_t* token, CStruct* args)
{
    const uint8_t* p = token + 1;
    for (;;) {
        p = SkipEndOfLinesIn(p);
        p = ResolveRandomJumps(p);
        uint8_t t = *p;
        if (t == 0x04)
            return p + 1;
        switch (t) {
        case 0x01:
        case 0x02:
            p = SkipEndOfLinesIn(p);
            break;
        case 0x03:
            p = ParseStructure(dest, p, args);
            break;
        case 0x09:
            ++p;
            break;
        case 0x16: {
            uint32_t name = Read4Bytes(p + 1);
            const uint8_t* next = ResolveRandomJumps(SkipEndOfLinesIn(p + 5));
            if (*next != 0x07) {
                p = AddParameter(p, 0, dest, args);
                break;
            }
            next = ResolveRandomJumps(SkipEndOfLinesIn(next + 1));
            if (*next == 0x0E)
                p = AddNamedExpression(next, name, dest, args);
            else
                p = AddParameter(next, name, dest, args);
            break;
        }
        default:
            if (t == 0x0E)
                p = AddExpression(p, dest, args);
            else
                p = AddParameter(p, 0, dest, args);
            break;
        }
    }
}

// 0x00471B00
const uint8_t* ParseArray(CArray* dest, const uint8_t* token, CStruct* args)
{
    const uint8_t* first = token + 1;
    const uint8_t* p = ResolveRandomJumps(first);
    uint32_t type = 0;
    uint32_t count = 0;
    if (*p == 0x06)
        return p + 1;
    do {
        uint8_t t = *p;
        switch (t) {
        case 0x03: type = ESYMBOLTYPE_STRUCTURE; break;
        case 0x05: type = ESYMBOLTYPE_ARRAY; break;
        case 0x16: type = ESYMBOLTYPE_NAME; break;
        case 0x17: case 0x18:
            if (type != ESYMBOLTYPE_FLOAT)
                type = ESYMBOLTYPE_INTEGER;
            break;
        case 0x1A: type = ESYMBOLTYPE_FLOAT; break;
        case 0x1B: type = ESYMBOLTYPE_STRING; break;
        case 0x1C: type = ESYMBOLTYPE_LOCALSTRING; break;
        case 0x1E: type = ESYMBOLTYPE_VECTOR; break;
        case 0x1F: type = ESYMBOLTYPE_PAIR; break;
        default: break;
        }
        switch (t) {
        case 0x01: case 0x02: case 0x09:
            p = SkipToken(p);
            break;
        case 0x03: case 0x05: case 0x16: case 0x17: case 0x18: case 0x1A: case 0x1B: case 0x1C: case 0x1E:
        case 0x1F:
            ++count;
            p = SkipValue(p);
            break;
        default:
            // (anything else doesn't move on: the original loops here)
            break;
        }
        p = ResolveRandomJumps(p);
    } while (*p != 0x06);
    if (type == 0)
        return p + 1;

    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    dest->SetSizeAndType(count, static_cast<ESymbolType>(type));
    Mem::PopContext();
    if (type == 7 || type == 8 || type == 9 || type == 11)
        return token;
    uint32_t* element = dest->GetArrayPointer();
    p = ResolveRandomJumps(first);
    while (*p != 0x06) {
        uint8_t t = *p;
        switch (type) {
        case ESYMBOLTYPE_STRUCTURE:
            // (no step first; unknown tokens don't move on: the original loops here)
            if (t == 0x01 || t == 0x09) {
                ++p;
            } else if (t == 0x02) {
                p += 5;
            } else if (t == 0x03) {
                CStruct* structure = NewInitStruct();
                p = ParseStructure(structure, p, args);
                *element++ = reinterpret_cast<uint32_t>(structure);
            }
            break;
        case ESYMBOLTYPE_ARRAY:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x05) {
                CArray* array = NewInitArray();
                p = ParseArray(array, p - 1, args);
                *element++ = reinterpret_cast<uint32_t>(array);
            }
            break;
        case ESYMBOLTYPE_INTEGER:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x17 || t == 0x18) {
                *element++ = Read4Bytes(p);
                p += 4;
            }
            break;
        case ESYMBOLTYPE_FLOAT:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x17 || t == 0x18) {
                float value = static_cast<float>(static_cast<int32_t>(Read4Bytes(p)));
                std::memcpy(element++, &value, 4);
                p += 4;
            } else if (t == 0x1A) {
                *element++ = Read4Bytes(p);
                p += 4;
            }
            break;
        case ESYMBOLTYPE_NAME:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x16) {
                *element++ = Read4Bytes(p);
                p += 4;
            }
            break;
        case ESYMBOLTYPE_STRING:
        case ESYMBOLTYPE_LOCALSTRING:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == (type == ESYMBOLTYPE_STRING ? 0x1B : 0x1C)) {
                uint32_t length = Read4Bytes(p);
                p += 4;
                *element++ = reinterpret_cast<uint32_t>(CreateString(reinterpret_cast<const char*>(p)));
                p += length;
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x1E) {
                CVector* vector = NewVector();
                vector = vector ? vector->Init() : nullptr;
                uint32_t x = Read4Bytes(p), y = Read4Bytes(p + 4), z = Read4Bytes(p + 8);
                std::memcpy(&vector->x, &x, 4);
                std::memcpy(&vector->y, &y, 4);
                std::memcpy(&vector->z, &z, 4);
                p += 12;
                *element++ = reinterpret_cast<uint32_t>(vector);
            }
            break;
        case ESYMBOLTYPE_PAIR:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x1F) {
                CPair* pair = NewPair();
                pair = pair ? pair->Init() : nullptr;
                uint32_t x = Read4Bytes(p), y = Read4Bytes(p + 4);
                std::memcpy(&pair->x, &x, 4);
                std::memcpy(&pair->y, &y, 4);
                p += 8;
                *element++ = reinterpret_cast<uint32_t>(pair);
            }
            break;
        default:
            break;
        }
        p = ResolveRandomJumps(p);
    }
    return p + 1;
}

// 0x004712B0
const uint8_t* ParseExpression(const uint8_t* token, CStruct* args, CComponent* result)
{
    CExpressionEvaluator& evaluator = Evaluator();
    CComponent& value = Scratch();
    evaluator.ClearIfNeeded();
    evaluator.SetTokenPointer(token);
    const uint8_t* p = token + 1;
    int parentheses = 0;
    bool afterDot = false;  // a name after '.' is a member name, not a global value
    bool haveValue = false; // (for a negative number to be taken as a subtraction)
    bool expectValue = true;
    auto pushed = [&] {
        haveValue = true;
        expectValue = false;
    };
    for (;;) {
        p = ResolveRandomJumps(p);
        uint8_t t = *p;
        switch (t) {
        case 0x01: case 0x06:
            ++p;
            continue;
        case 0x02:
            p += 5;
            continue;
        case 0x03: case 0x1B: case 0x1C: case 0x1E: case 0x1F: case 0x30:
            if (!expectValue)
                break;
            p = ParseValue(p, args, &value);
            evaluator.Input(&value);
            CleanUpComponent(&value);
            pushed();
            continue;
        case 0x05: case 0x07: case 0x08: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x12: case 0x14:
        case 0x32: case 0x33: // operators
            evaluator.Input(static_cast<int>(t));
            ++p;
            afterDot = t == 0x08;
            haveValue = false;
            expectValue = true;
            continue;
        case 0x0E:
            if (!expectValue)
                break;
            ++p;
            evaluator.OpenParenthesis();
            ++parentheses;
            haveValue = false;
            expectValue = true;
            continue;
        case 0x0F:
            ++p;
            if (parentheses == 0)
                break;
            evaluator.CloseParenthesis();
            expectValue = false;
            --parentheses;
            haveValue = true;
            continue;
        case 0x16: {
            if (!expectValue)
                break;
            p = ParseValue(p, args, &value);
            CSymbolTableEntry* symbol = afterDot ? nullptr : LookUpSymbol(value.checksum);
            if (symbol) {
                bool push = true;
                switch (symbol->type) {
                case ESYMBOLTYPE_INTEGER: case ESYMBOLTYPE_FLOAT: case ESYMBOLTYPE_STRING:
                case ESYMBOLTYPE_LOCALSTRING: case ESYMBOLTYPE_PAIR: case ESYMBOLTYPE_VECTOR: case ESYMBOLTYPE_NAME:
                    SetType(&value, symbol->type);
                    value.raw = symbol->raw;
                    break;
                case ESYMBOLTYPE_STRUCTURE:
                    SetType(&value, ESYMBOLTYPE_STRUCTUREPOINTER);
                    value.raw = symbol->raw;
                    break;
                case ESYMBOLTYPE_ARRAY:
                    SetType(&value, ESYMBOLTYPE_ARRAYPOINTER);
                    value.raw = symbol->raw;
                    break;
                case ESYMBOLTYPE_CFUNCTION: {
                    // Called now, with the arguments that follow.
                    CScript* script = GetCurrentScript();
                    SetType(&value, ESYMBOLTYPE_INTEGER);
                    CStruct* params = NewInitStruct();
                    p = ParseArguments(params, p, args);
                    // (the byte the function returns, as it is)
                    using Function = uint8_t(__cdecl*)(CStruct*, CScript*);
                    value.raw = reinterpret_cast<Function>(symbol->raw)(params, script);
                    if (params) {
                        params->Clear();
                        DeleteStruct(params);
                    }
                    break;
                }
                case ESYMBOLTYPE_MEMBERFUNCTION:
                    push = false;
                    break;
                default:
                    break;
                }
                if (push)
                    evaluator.Input(&value);
            } else {
                evaluator.Input(&value);
            }
            value.typeAndFlag &= 1;
            value.raw = 0;
            pushed();
            continue;
        }
        case 0x17:
        case 0x1A: {
            if (expectValue) {
                p = ParseValue(p, args, &value);
                evaluator.Input(&value);
                CleanUpComponent(&value);
                pushed();
                continue;
            }
            // A negative number right after a value: a subtraction ("a -1" is compiled as "a" "-1").
            const uint8_t* next = ParseValue(p, args, &value);
            bool negative = t == 0x17 ? value.integer < 0 : value.floatValue < 0.0f;
            if (!negative || !haveValue)
                break;
            evaluator.Input(static_cast<int>(OPERATOR_MINUS));
            if (t == 0x17)
                value.integer = -value.integer;
            else
                value.floatValue = -value.floatValue;
            evaluator.Input(&value);
            CleanUpComponent(&value);
            p = next;
            pushed();
            continue;
        }
        case 0x2C: // <...>
            if (!expectValue)
                break;
            ++p;
            if (args) {
                SetType(&value, ESYMBOLTYPE_STRUCTUREPOINTER);
                value.structure = args;
                evaluator.Input(&value);
            }
            pushed();
            continue;
        case 0x2D: { // <name>
            if (!expectValue)
                break;
            uint32_t name = Read4Bytes(p + 2);
            p += 6;
            CComponent* found = args ? args->FindLastComponent(name) : nullptr;
            if (found) {
                value.typeAndFlag = static_cast<uint8_t>((value.typeAndFlag & 1) | (found->typeAndFlag & 0xFE));
                value.raw = found->raw;
                ResolveNameComponent(&value);
                MakeReference(&value);
                evaluator.Input(&value);
                value.typeAndFlag &= 1;
                value.raw = 0;
            }
            pushed();
            continue;
        }
        default:
            // (other tokens don't move on: the original loops here)
            continue;
        }
        break; // the end of the expression
    }
    evaluator.GetResult(result);
    return p;
}

} // namespace Script

// --- the entry points with register arguments -------------------------------------------------------------

namespace {

using Script::CComponent;
using Script::CStruct;

const Script::CStoredRandom* __cdecl FindStoredRandomRegisters(const uint8_t* table, uint32_t type, uint32_t count)
{
    return Script::FindStoredRandom(table, type, count);
}
const uint8_t* __cdecl SkipValueRegisters(const uint8_t* token) { return Script::SkipValue(token); }
const uint8_t* __cdecl ParseRandomRangeRegisters(const uint8_t* token, CComponent* out, uint32_t deterministic)
{
    return Script::ParseRandomRange(token, out, (deterministic & 0xFF) != 0);
}
const uint8_t* __cdecl AddParameterRegisters(const uint8_t* token, uint32_t name, CStruct* dest, CStruct* args)
{
    return Script::AddParameter(token, name, dest, args);
}

} // namespace

// 0x0046F7A0: the table in EBX, the type in EDI, the count in ESI.
__declspec(naked) void Script::FindStoredRandomStub()
{
    __asm {
        push esi
        push edi
        push ebx
        call FindStoredRandomRegisters
        add esp, 12
        ret
    }
}

// 0x0046FD60: the token in EAX.
__declspec(naked) void Script::SkipValueStub()
{
    __asm {
        push eax
        call SkipValueRegisters
        add esp, 4
        ret
    }
}

// 0x0046FBD0: the token in EAX, the stream choice in BL, the component on the stack (the caller pops it).
__declspec(naked) void Script::ParseRandomRangeStub()
{
    __asm {
        movzx ecx, bl
        push ecx
        push dword ptr [esp + 8]
        push eax
        call ParseRandomRangeRegisters
        add esp, 12
        ret
    }
}

// 0x00470130: the token in ECX, the name in EDX, the structures on the stack (the caller pops them).
__declspec(naked) void Script::AddParameterStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        push ecx
        call AddParameterRegisters
        add esp, 16
        ret
    }
}

REPLACE(0x004728C0, Script::GetCurrentScript);
REPLACE(0x004728D0, Script::GetNextScript);
REPLACE(0x00470BB0, Script::NewStoredRandom);
REPLACE(0x00470C60, Script::ResolveRandomJumps);
REPLACE(0x00479730, Script::ResolveNameComponent);
REPLACE(0x004706F0, Script::ParseValue);
REPLACE(0x00470F30, Script::ParseArguments);
REPLACE(0x00471860, Script::ParseStructure);
REPLACE(0x00471B00, Script::ParseArray);
REPLACE(0x004712B0, Script::ParseExpression);
REPLACE_UNCHECKED(0x0046F7A0, Script::FindStoredRandomStub);   // arguments in EBX, EDI, ESI
REPLACE_UNCHECKED(0x0046FD60, Script::SkipValueStub);          // argument in EAX
REPLACE_UNCHECKED(0x0046FBD0, Script::ParseRandomRangeStub);   // arguments in EAX, BL and on the stack
REPLACE_UNCHECKED(0x00470130, Script::AddParameterStub);       // arguments in ECX, EDX and on the stack
