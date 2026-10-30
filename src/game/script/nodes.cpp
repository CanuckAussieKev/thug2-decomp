// SkateScript's node functions: a node's name, links, position, angles and orientation, by its structure or by its
// name (nodes.h).

#include "game/script/nodes.h"

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/script/node_manager.h"
#include "game/script/cache.h"
#include "game/script/node_prefixes.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/script_cfuncs.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace SkateScript {

using Script::CArray;
using Script::CStruct;
using Script::VectorValue;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;        // name
constexpr uint32_t kLinks = 0x2E7D5EE7;       // links
constexpr uint32_t kPos = 0x7F261953;         // pos
constexpr uint32_t kPosition = 0xB9D31B0A;    // position
constexpr uint32_t kAngles = 0x9D2D0915;      // angles
constexpr uint32_t kOrientation = 0xC97F3AA9; // orientation
constexpr uint32_t kScr = 0xA6D2D890;         // scr (SetEventHandler's script)
constexpr uint32_t kEndGap = 0xE5399FB2;      // EndGap
constexpr uint32_t kGapScript = 0x7C9E51AB;   // gapscript
// SetEventHandler (the C function, as tokens 0x43 hold it: the game's address)
constexpr uint32_t kSetEventHandler = 0x0044AB20;
constexpr int kMaxDepth = 5;

// Script tokens (qb.h).
constexpr uint8_t kTokenEquals = 0x07;
constexpr uint8_t kTokenOpenParenthesis = 0x0E;
constexpr uint8_t kTokenName = 0x16;
constexpr uint8_t kTokenRepeat = 0x21;
constexpr uint8_t kTokenEndOfScript = 0x24;
constexpr uint8_t kTokenNot = 0x39;
constexpr uint8_t kTokenCFunction = 0x43;        // a C function already looked up
constexpr uint8_t kTokenMemberFunction = 0x44;   // a member function's name (no table entry)
constexpr uint8_t kTokenMemberFunctionEntry = 0x46; // a member function's table entry (its name first)
constexpr uint8_t kTokenIf = 0x47;               // (a short jump)

CNodeManager* Manager() { return NodeManagerInstance(); }

VectorValue* AsValue(Mth::Vector* v) { return reinterpret_cast<VectorValue*>(v); }

// A quaternion (x, y, z, w) to a rotation matrix (0x0042BC40, Mth::Quat::GetMatrix).
void QuatToMatrix(const float* quat, Mth::Matrix* matrix) { reinterpret_cast<const Mth::Quat*>(quat)->GetMatrix(matrix); }

} // namespace

// 0x0045BE40
uint32_t GetNodeName(CStruct* node)
{
    uint32_t name = 0;
    node->GetChecksum(kName, &name, 0);
    return name;
}

// 0x0045C020
uint32_t GetLinkCount(CStruct* node)
{
    CArray* links = nullptr;
    node->GetArray(kLinks, &links, 0);
    return links ? links->m_size : 0;
}

// 0x0045BE10
uint32_t GetLink(CStruct* node, int index)
{
    CArray* links = nullptr;
    node->GetArray(kLinks, &links, 0);
    return links->GetElement(index); // (no check that there were links)
}

// 0x0045C420
CStruct* GetLinkedNode(CStruct* node, int index) { return Manager()->GetNode(GetLink(node, index), true); }

// 0x0045C4B0
bool IsLinkedTo(CStruct* node, CStruct* target)
{
    CArray* links = nullptr;
    node->GetArray(kLinks, &links, 0);
    if (!links)
        return false;
    int count = static_cast<int>(links->m_size);
    for (int i = 0; i < count; ++i) {
        if (Manager()->GetNode(links->GetElement(i), true) == target)
            return true;
    }
    return false;
}

// 0x0045C050
bool GetPosition(CStruct* node, Mth::Vector* out)
{
    if (node->GetVector(kPos, AsValue(out), 0))
        return true;
    if (node->GetVector(kPosition, AsValue(out), 0)) {
        out->z = -out->z; // (the files' position= is in the exporter's coordinates)
        return true;
    }
    *out = {0.0f, 0.0f, 0.0f, 1.0f};
    return false;
}

// 0x0045C180
bool GetAngles(CStruct* node, Mth::Vector* out)
{
    *out = {0.0f, 0.0f, 0.0f, 1.0f};
    return node->GetVector(kAngles, AsValue(out), 0);
}

// 0x0045C0B0
void GetOrientation(CStruct* node, Mth::Matrix* out)
{
    VectorValue v;
    if (!node->GetVector(kOrientation, &v, 0)) {
        for (int r = 0; r < 4; ++r)
            out->row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
        return;
    }
    // (x87, 53-bit: the squares summed z, y, x in double, w = sqrt(1 - min(sum, 1)) rounded once)
    double sum = static_cast<double>(v.z) * v.z + static_cast<double>(v.y) * v.y;
    sum += static_cast<double>(v.x) * v.x;
    if (sum > 1.0)
        sum = 1.0;
    const float quat[4] = {v.x, v.y, v.z, static_cast<float>(std::sqrt(1.0 - sum))};
    QuatToMatrix(quat, out);
}

// 0x0045C1B0
CStruct* GetNode(uint32_t name, bool assert) { return Manager()->GetNode(name, assert); }

// 0x0045BE70
bool NodeExists(uint32_t name) { return Manager()->GetNode(name, false) != nullptr; }

// 0x0045C3A0
uint32_t GetLinkCount(uint32_t name) { return GetLinkCount(Manager()->GetNode(name, true)); }

// 0x0045C3E0
uint32_t GetLink(uint32_t name, int index) { return GetLink(Manager()->GetNode(name, true), index); }

// 0x0045C460
CStruct* GetLinkedNode(uint32_t name, int index)
{
    return GetLinkedNode(Manager()->GetNode(name, true), index);
}

// 0x0045C560
bool GetPosition(uint32_t name, Mth::Vector* out) { return GetPosition(Manager()->GetNode(name, true), out); }

// 0x0045C590
bool GetAngles(uint32_t name, Mth::Vector* out) { return GetAngles(Manager()->GetNode(name, true), out); }

// 0x0045C520
bool IsLinkedTo(uint32_t name, uint32_t target)
{
    CStruct* node = Manager()->GetNode(name, true);
    return IsLinkedTo(node, Manager()->GetNode(target, true));
}

// 0x0045C2D0
CStruct* GetNearestNodeByPrefix(uint32_t prefix, const Mth::Vector* pos)
{
    CStruct* nearest = nullptr;
    float best = 1e20f;
    uint16_t count = 0;
    const uint32_t* names = NodePrefixes()->GetNodes(prefix, &count);
    if (!count)
        return nullptr;
    // (The original's position isn't reset between nodes, nor set before the first: a node without pos= is
    // measured where the one before it was. Here the first such node is at the origin.)
    VectorValue p = {0.0f, 0.0f, 0.0f, 0.0f};
    for (uint32_t i = count; i; --i, ++names) {
        CStruct* node = Manager()->GetNode(*names, true);
        node->GetVector(kPos, &p, 0);
        // (x87, 53-bit: the differences and squares in double, summed z, y, x)
        double dx = static_cast<double>(p.x) - pos->x;
        double dy = static_cast<double>(p.y) - pos->y;
        double dz = static_cast<double>(p.z) - pos->z;
        double distance = dz * dz + dy * dy;
        distance += dx * dx;
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = node;
        }
    }
    return nearest;
}

// 0x0045C950
void ForEachScriptCall(uint32_t script, uint32_t target, ScriptCallCallback callback, CStruct* parentParams, int depth)
{
    if (depth > kMaxDepth)
        return;
    Script::CScriptCache* cache = Script::g_scriptCache;
    const uint8_t* token = cache->GetScript(script);
    CStruct* params = nullptr;
    if (void* memory = Script::NewStruct())
        params = static_cast<CStruct*>(memory)->Init();
    token = Script::SkipToEndOfStatement(token); // (past the script's first line)
    for (;;) {
        const uint8_t* call = token;
        switch (*token) {
        case kTokenName:
        case kTokenMemberFunction:
        case kTokenMemberFunctionEntry: {
            // A name at the start of a statement (or one already looked up): a script is followed into, a C or
            // member function that is `target` handed to the callback; EndGap's gapscript= is followed too.
            uint32_t name = *token == kTokenMemberFunctionEntry
                                ? *reinterpret_cast<const uint32_t*>(Script::Read4Bytes(token + 1))
                                : Script::Read4Bytes(token + 1);
            token += 5;
            if (*token == kTokenEquals)
                break;
            Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(name);
            if (!symbol)
                break;
            switch (symbol->type) {
            case Script::ESYMBOLTYPE_QSCRIPT:
                params->Clear();
                token = Script::ParseArguments(params, token, parentParams);
                if (name == target)
                    callback(params, call);
                else
                    token = Script::SkipToEndOfStatement(token);
                ForEachScriptCall(name, target, callback, params, depth + 1);
                break;
            case Script::ESYMBOLTYPE_CFUNCTION:
                if (name != target) {
                    token = Script::SkipToEndOfStatement(token);
                    break;
                }
                params->Clear();
                token = Script::ParseArguments(params, token, parentParams);
                callback(params, call);
                break;
            case Script::ESYMBOLTYPE_MEMBERFUNCTION:
                if (name != target) {
                    token = Script::SkipToEndOfStatement(token);
                    break;
                }
                params->Clear();
                token = Script::ParseArguments(params, token, parentParams);
                callback(params, call);
                if (name == kEndGap) {
                    uint32_t gapScript = 0;
                    if (params->GetChecksum(kGapScript, &gapScript, 0))
                        ForEachScriptCall(gapScript, target, callback, nullptr, depth + 1);
                }
                break;
            default:
                break;
            }
            break;
        }
        case kTokenCFunction: {
            // A C function already looked up: SetEventHandler's scr= is followed.
            uint32_t function = Script::Read4Bytes(token + 1);
            token += 5;
            // (the game's CFunction table holds the game's address; a table of ours would hold our function)
            if (function != kSetEventHandler &&
                function != reinterpret_cast<uint32_t>(&CFuncs::CFunc_SetEventHandler)) {
                token = Script::SkipToEndOfStatement(token);
                break;
            }
            params->Clear();
            token = Script::ParseArguments(params, token, parentParams);
            uint32_t handler = 0;
            params->GetChecksum(kScr, &handler, 0);
            if (handler)
                ForEachScriptCall(handler, target, callback, nullptr, depth + 1);
            break;
        }
        case kTokenRepeat:
            token = Script::SkipToEndOfStatement(Script::SkipToken(token));
            break;
        case kTokenIf:
            token = Script::SkipToken(token);
            if (*token == kTokenNot)
                token = Script::SkipToken(token);
            if (*token == kTokenOpenParenthesis)
                token = Script::SkipToEndOfStatement(token);
            break;
        case kTokenEndOfScript:
            cache->ReleaseScript(script);
            if (params) {
                params->Clear();
                Script::DeleteStruct(params);
            }
            return;
        default:
            token = Script::SkipToken(token);
            break;
        }
    }
}

// 0x0045CCA0
void ForEachNodeScriptCall(uint32_t scriptParam, uint32_t target, ScriptCallCallback callback)
{
    Script::ClearScriptFlag6F2564();
    Manager()->StartWalk(0);
    while (CStruct* node = Manager()->NextNode()) {
        uint32_t script = 0;
        if (node->GetChecksum(scriptParam, &script, 0))
            ForEachScriptCall(script, target, callback, nullptr, 0);
    }
    Manager()->EndWalk();
    Script::SetScriptFlag6F2564();
}

// 0x0045C1D0
void StartNodeWalk(uint32_t name) { Manager()->StartWalk(name); }
// 0x0045C1F0
CStruct* NextNode() { return Manager()->NextNode(); }
// 0x0045C200
void EndNodeWalk() { Manager()->EndWalk(); }
// 0x0045C210
CArray* GetLastNodeArray() { return Manager()->GetLastNodeArray(); }
// 0x0045C220
uint32_t GetNodeOwnerValue58() { return Manager()->GetOwnerValue58(); }
// 0x0045C230
uint32_t GetLastNodeArrayName() { return Manager()->GetLastNodeArrayName(); }

} // namespace SkateScript

REPLACE(0x0045BE40, SkateScript::GetNodeName);
REPLACE(0x0045C020, static_cast<uint32_t (*)(Script::CStruct*)>(SkateScript::GetLinkCount));
REPLACE(0x0045BE10, static_cast<uint32_t (*)(Script::CStruct*, int)>(SkateScript::GetLink));
REPLACE(0x0045C420, static_cast<Script::CStruct* (*)(Script::CStruct*, int)>(SkateScript::GetLinkedNode));
REPLACE(0x0045C4B0, static_cast<bool (*)(Script::CStruct*, Script::CStruct*)>(SkateScript::IsLinkedTo));
REPLACE(0x0045C050, static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(SkateScript::GetPosition));
REPLACE(0x0045C180, static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(SkateScript::GetAngles));
REPLACE(0x0045C0B0, SkateScript::GetOrientation);
REPLACE(0x0045C1B0, SkateScript::GetNode);
REPLACE(0x0045BE70, SkateScript::NodeExists);
REPLACE(0x0045C3A0, static_cast<uint32_t (*)(uint32_t)>(SkateScript::GetLinkCount));
REPLACE(0x0045C3E0, static_cast<uint32_t (*)(uint32_t, int)>(SkateScript::GetLink));
REPLACE(0x0045C460, static_cast<Script::CStruct* (*)(uint32_t, int)>(SkateScript::GetLinkedNode));
REPLACE(0x0045C560, static_cast<bool (*)(uint32_t, Mth::Vector*)>(SkateScript::GetPosition));
REPLACE(0x0045C590, static_cast<bool (*)(uint32_t, Mth::Vector*)>(SkateScript::GetAngles));
REPLACE(0x0045C520, static_cast<bool (*)(uint32_t, uint32_t)>(SkateScript::IsLinkedTo));
REPLACE(0x0045C2D0, SkateScript::GetNearestNodeByPrefix);
REPLACE(0x0045C1D0, SkateScript::StartNodeWalk);
REPLACE(0x0045C1F0, SkateScript::NextNode);
REPLACE(0x0045C200, SkateScript::EndNodeWalk);
REPLACE(0x0045C210, SkateScript::GetLastNodeArray);
REPLACE(0x0045C220, SkateScript::GetNodeOwnerValue58);
REPLACE(0x0045C230, SkateScript::GetLastNodeArrayName);
REPLACE(0x0045C950, SkateScript::ForEachScriptCall);
REPLACE(0x0045CCA0, SkateScript::ForEachNodeScriptCall);
