// NOT ORIGINAL - the installed network transport (transport.h).

#include "engine/net/transport.h"

namespace net {

namespace {

// The transport app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Transport*& installed()
{
    static Transport* transport = nullptr;
    return transport;
}

} // namespace

Transport& transport() { return *installed(); }

void install_transport(Transport& transport) { installed() = &transport; }

} // namespace net
