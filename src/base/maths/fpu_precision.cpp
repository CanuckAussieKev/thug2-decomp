#include "base/maths/fpu_precision.h"

#if defined(_M_IX86) || defined(__i386__)
#include <cfloat> // IWYU pragma: keep - float.h's __control87_2 and its _PC_ / _RC_ / _IC_ / _MCW_ values
#endif

namespace maths {

void use_device_fpu_precision()
{
#if defined(_M_IX86) || defined(__i386__)
    // The x87's word only (no SSE pointer: MXCSR stays as it is, as Direct3D 9 leaves it): 24 bits, round to
    // nearest, projective infinity, every exception masked - the x87 word 0x007F. (__control87_2: the C runtime's,
    // in Microsoft's and MinGW-w64's float.h alike.)
    static_assert(_RC_NEAR == 0 && _IC_PROJECTIVE == 0); // (round to nearest, projective: the fields' zeros)
    constexpr unsigned int kWord = _PC_24 | _MCW_EM;
    constexpr unsigned int kFields = _MCW_PC | _MCW_RC | _MCW_IC | _MCW_EM;
    unsigned int x87 = 0;
    __control87_2(kWord, kFields, &x87, nullptr);
#endif
}

} // namespace maths
