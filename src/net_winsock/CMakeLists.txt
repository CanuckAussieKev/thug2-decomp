# The Winsock network backend (src/net_winsock): the game's sockets - made, made non-blocking, allowed to broadcast,
# bound, connected, sent and received on, closed - and its address and host-name lookups, as the game called them. It
# implements net::Transport (engine/net/transport.h); app/ installs it (app/main.cpp). It includes the engine and the
# layers below it (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without
# Winsock. (The GameSpy SDK keeps its own sockets: platform/gamespy.cpp, cmake/gamespy_sdk.cmake.)
# NOT ORIGINAL - a portable build takes port/posix_transport.cpp (the same net::Transport over BSD sockets) in place of
# Winsock's calls. The layer's name stays: it is the transport the exe installs.
add_library(thug2_net_winsock STATIC
    winsock_transport.cpp
)
# NOT ORIGINAL - a portable build compiles port/posix_transport.cpp instead (BSD sockets behind the same seam).
if(THUG2_PORTABLE)
    set_source_files_properties(winsock_transport.cpp PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(thug2_net_winsock PRIVATE ${PROJECT_SOURCE_DIR}/port/posix_transport.cpp)
    target_link_libraries(thug2_net_winsock PUBLIC thug2_engine)
else()
# ws2_32: Winsock
target_link_libraries(thug2_net_winsock PUBLIC thug2_engine PRIVATE ws2_32)
# It calls Winsock's IPv4-only functions the game uses (inet_addr, inet_ntoa, gethostbyname): Winsock marks them
# deprecated unless this is defined.
set_source_files_properties(winsock_transport.cpp PROPERTIES COMPILE_DEFINITIONS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
