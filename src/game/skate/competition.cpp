// Mdl::CCompetition: the pieces at 0x0054FFD0-0x0055002F (see competition.h).

#include "game/skate/competition.h"

#include <new>

#include "game/script/struct.h"
#include "hook/hook.h"

namespace Mdl {

// 0x0054FFD0
CCompetition::Leader* CCompetition::Leader::Construct()
{
    new (&m_name) Str::String();
    return this;
}

// 0x0054FFF0
void CCompetition::DestroyBody()
{
    // (the compiler's array-destructor helper, 0x0062382B: last to first)
    for (int i = 7; i >= 0; --i)
        m_leaders[i].m_name.~String();
}

// 0x00550010
void CCompetition::EditParams(Script::CStruct* params) { m_params->AppendStructureIfAny(params); }

// 0x00550020
void CCompetition::StartRun()
{
    ++m_runs;
    m_unknown150 = 0;
    m_unknown124 = 0;
}

} // namespace Mdl

REPLACE(0x0054FFD0, &Mdl::CCompetition::Leader::Construct);
REPLACE(0x0054FFF0, &Mdl::CCompetition::DestroyBody);
REPLACE(0x00550010, &Mdl::CCompetition::EditParams);
REPLACE(0x00550020, &Mdl::CCompetition::StartRun);
