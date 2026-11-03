#pragma once

namespace core {

// NOT ORIGINAL - a callback whose arguments reach the game as one record (Core Guideline I.24: the GameSpy SDK's
// callbacks and the file system's completion hooks take long runs of ints and pointers, easily swapped).
// `gathered<Record, Handler>` has the caller's signature - deduced from the callback pointer it is stored in, or
// spelled out where only an address is taken - and calls Handler with the arguments, in order, as a Record (whose
// fields follow the caller's argument list).
template <typename Record, auto Handler, typename Return, typename... Args>
Return gathered(Args... args)
{
    return Handler(Record{args...});
}

} // namespace core
