# The network messages: the packet framing and every game message, byte-compatible with the original's peers.
add_library(thug2_wire STATIC
    gamenet_messages.cpp
    net_wire.cpp
    play_messages.cpp
    skate_net_messages.cpp
    skater_messages.cpp
)
target_link_libraries(thug2_wire PUBLIC thug2_formats)
