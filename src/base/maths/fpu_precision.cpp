#include "base/maths/fpu_precision.h"

#if defined(_M_IX86) || defined(__i386__)
#include <cfloat> // IWYU pragma: keep - float.h's __control87_2 and its _PC_ / _RC_ / _IC_ / _MCW_ values
#endif

namespace maths {

namespace {

#if defined(_M_IX86) || defined(__i386__)
// Every field of the x87's word: precision, rounding, infinity, the exception masks. (__control87_2 with no SSE
// pointer: the x87's word only - MXCSR stays as it is, as Direct3D 9 leaves it. It is the C runtime's, in Microsoft's
// and MinGW-w64's float.h alike.)
constexpr unsigned int kX87Fields = _MCW_PC | _MCW_RC | _MCW_IC | _MCW_EM;
#endif

} // namespace

void use_device_fpu_precision()
{
#if defined(_M_IX86) || defined(__i386__)
    // 24 bits, round to nearest, projective infinity, every exception masked - the x87 word 0x007F.
    static_assert(_RC_NEAR == 0 && _IC_PROJECTIVE == 0); // (round to nearest, projective: the fields' zeros)
    constexpr unsigned int kWord = _PC_24 | _MCW_EM;
    unsigned int x87 = 0;
    __control87_2(kWord, kX87Fields, &x87, nullptr);
#endif
}

unsigned int fpu_word()
{
#if defined(_M_IX86) || defined(__i386__)
    unsigned int x87 = 0;
    __control87_2(0, 0, &x87, nullptr);
    return x87;
#else
    return 0;
#endif
}

void restore_fpu_word(unsigned int word)
{
#if defined(_M_IX86) || defined(__i386__)
    unsigned int x87 = 0;
    __control87_2(word, kX87Fields, &x87, nullptr);
#else
    (void)word;
#endif
}

} // namespace maths
