# The macOS (arm64) port's network backend (src/net_bsd): the game's sockets over BSD sockets, in place of
# src/net_winsock's Winsock calls - made, made non-blocking, allowed to broadcast, bound, connected, sent and received
# on, closed - and its address and host-name lookups. It implements net::Transport (engine/net/transport.h); app/
# installs it (THUG2_PORTABLE). It includes the engine and the layers below it (tools/layers.ts checks it).
add_library(thug2_net_bsd STATIC
    bsd_transport.cpp
)
target_link_libraries(thug2_net_bsd PUBLIC thug2_engine)
