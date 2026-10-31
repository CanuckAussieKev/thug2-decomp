# The GameSpy SDK the game links: IGN's
# open-source release (BSD-3, "Copyright (c) 2011, IGN Entertainment, Inc." - see THIRD_PARTY_NOTICES.md), the
# December 2007 SDK (common 2.06), from github.com/nitrocaster/GameSpy at a pinned commit; downloaded at configure
# time and checked against its SHA-256 - not stored in this repo. Only the modules the game uses are built, as the
# static C library `gamespy_sdk` (called through src/game/net/gamespy.h).
#
# Two changes are made to the source. The peer SDK's chat server name is a constant there ("peerchat."
# GSI_DOMAIN_NAME, Peer/peerOperations.c); it becomes a variable, peerChatServerAddress, like the SDK's other host
# names, so the online domain (OnlineDomain) can be set at run time (src/game/net/gamespy.cpp). And the chat SDK's
# nick checks (Chat/chatMain.h) are as the game's own copy of the SDK has them: that one checks nothing but "not
# empty" before sending the nick (the peer cuts it at 63 characters), while the 2007 SDK refuses nicks of 21 or more
# characters and any outside its character set - which lacks the backslash the game's nick rules allow - with a
# nick error the game's callback ignores (the connect would stall). So the length limit is the peer's own (64) and
# the backslash is valid.
#
# MinGW-w64: the SDK's win32 platform layer is plain Winsock / Win32 (no MSVC-only code in the modules built here,
# as far as this build shows - not tried yet); gsPlatform.h picks the platform from _WIN32.

FetchContent_Declare(gamespy_sdk
    URL https://github.com/nitrocaster/GameSpy/archive/d1deb2d1a951cf77933dda040b8d311cc09815a7.tar.gz
    URL_HASH SHA256=733d97d22139213de06905a4a3ad9095350b8c4dc5a0001184ddc496dd3d3640
    DOWNLOAD_NAME GameSpy-d1deb2d1a951cf77933dda040b8d311cc09815a7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(gamespy_sdk)
set(THUG2_GAMESPY_DIR "${gamespy_sdk_SOURCE_DIR}/src/GameSpy")

# The chat server's name made a variable (idempotent: the replaced text is gone once done).
set(_peer_ops "${THUG2_GAMESPY_DIR}/Peer/peerOperations.c")
file(READ "${_peer_ops}" _text)
set(_old "#define PI_CHAT_SERVER_ADDRESS    \"peerchat.\" GSI_DOMAIN_NAME")
string(FIND "${_text}" "${_old}" _at)
if(NOT _at EQUAL -1)
    string(REPLACE "${_old}"
        "char peerChatServerAddress[64] = \"peerchat.\" GSI_DOMAIN_NAME; /* (made a variable for thug2-decomp) */\n#define PI_CHAT_SERVER_ADDRESS    peerChatServerAddress"
        _text "${_text}")
    file(WRITE "${_peer_ops}" "${_text}")
elseif(NOT _text MATCHES "peerChatServerAddress")
    message(FATAL_ERROR "GameSpy SDK: Peer/peerOperations.c is not the expected source")
endif()

# The nick rules as the game's copy of the SDK has them (idempotent).
set(_chat_main "${THUG2_GAMESPY_DIR}/Chat/chatMain.h")
file(READ "${_chat_main}" _text)
set(_old_length "#define MAX_CHAT_NICK          21")
set(_new_length "#define MAX_CHAT_NICK          MAX_NICK /* (thug2-decomp: the peer's own limit) */")
string(FIND "${_text}" "${_old_length}" _at)
if(NOT _at EQUAL -1)
    string(REPLACE "${_old_length}" "${_new_length}" _text "${_text}")
    file(WRITE "${_chat_main}" "${_text}")
elseif(NOT _text MATCHES "thug2-decomp: the peer's own limit")
    message(FATAL_ERROR "GameSpy SDK: Chat/chatMain.h is not the expected source")
endif()
# (a backslash in the C string is written twice)
set(_old_chars [=[@[]^_`]=])
set(_new_chars [=[@[\\]^_`]=])
string(FIND "${_text}" "${_old_chars}" _at)
if(NOT _at EQUAL -1)
    string(REPLACE "${_old_chars}" "${_new_chars}" _text "${_text}")
    file(WRITE "${_chat_main}" "${_text}")
else()
    string(FIND "${_text}" "${_new_chars}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "GameSpy SDK: Chat/chatMain.h is not the expected source")
    endif()
endif()

set(_gs "${THUG2_GAMESPY_DIR}")
add_library(gamespy_sdk STATIC EXCLUDE_FROM_ALL
    # common
    # (nonport.c only includes the common/ files again: not built)
    ${_gs}/darray.c ${_gs}/hashtable.c ${_gs}/md5c.c
    ${_gs}/common/gsAssert.c ${_gs}/common/gsAvailable.c ${_gs}/common/gsCore.c ${_gs}/common/gsCrypt.c
    ${_gs}/common/gsDebug.c ${_gs}/common/gsLargeInt.c ${_gs}/common/gsMemory.c ${_gs}/common/gsPlatform.c
    ${_gs}/common/gsPlatformSocket.c ${_gs}/common/gsPlatformThread.c ${_gs}/common/gsPlatformUtil.c
    ${_gs}/common/gsRC4.c ${_gs}/common/gsSHA1.c ${_gs}/common/gsSSL.c ${_gs}/common/gsSoap.c
    ${_gs}/common/gsStringUtil.c ${_gs}/common/gsUdpEngine.c ${_gs}/common/gsXML.c
    # (common/win32/Win32Common.c is the samples' main(), not built; gsPlatform*.c include the other win32 files)
    # query and reporting, NAT negotiation, server browsing
    ${_gs}/qr2/qr2.c ${_gs}/qr2/qr2regkeys.c
    ${_gs}/natneg/natneg.c ${_gs}/natneg/NATify.c
    ${_gs}/serverbrowsing/sb_crypt.c ${_gs}/serverbrowsing/sb_queryengine.c ${_gs}/serverbrowsing/sb_server.c
    ${_gs}/serverbrowsing/sb_serverbrowsing.c ${_gs}/serverbrowsing/sb_serverlist.c
    # chat and peer (lobbies, game listing, hosting), pinger (peer's pings)
    ${_gs}/Chat/chatCallbacks.c ${_gs}/Chat/chatChannel.c ${_gs}/Chat/chatCrypt.c ${_gs}/Chat/chatHandlers.c
    ${_gs}/Chat/chatMain.c ${_gs}/Chat/chatSocket.c
    ${_gs}/Peer/peerAutoMatch.c ${_gs}/Peer/peerCallbacks.c ${_gs}/Peer/peerGlobalCallbacks.c ${_gs}/Peer/peerHost.c
    ${_gs}/Peer/peerKeys.c ${_gs}/Peer/peerMain.c ${_gs}/Peer/peerMangle.c ${_gs}/Peer/peerOperations.c
    ${_gs}/Peer/peerPing.c ${_gs}/Peer/peerPlayers.c ${_gs}/Peer/peerQR.c ${_gs}/Peer/peerRooms.c
    ${_gs}/Peer/peerSB.c
    ${_gs}/pinger/pingerMain.c
    # presence (GP; its peer messages go through gsUdpEngine, which is built on gt2)
    ${_gs}/GP/gp.c ${_gs}/GP/gpi.c ${_gs}/GP/gpiBuddy.c ${_gs}/GP/gpiBuffer.c ${_gs}/GP/gpiCallback.c
    ${_gs}/GP/gpiConnect.c ${_gs}/GP/gpiInfo.c ${_gs}/GP/gpiKeys.c ${_gs}/GP/gpiOperation.c ${_gs}/GP/gpiPeer.c
    ${_gs}/GP/gpiProfile.c ${_gs}/GP/gpiSearch.c ${_gs}/GP/gpiTransfer.c ${_gs}/GP/gpiUnique.c
    ${_gs}/GP/gpiUtility.c
    ${_gs}/gt2/gt2Auth.c ${_gs}/gt2/gt2Buffer.c ${_gs}/gt2/gt2Callback.c ${_gs}/gt2/gt2Connection.c
    ${_gs}/gt2/gt2Encode.c ${_gs}/gt2/gt2Filter.c ${_gs}/gt2/gt2Main.c ${_gs}/gt2/gt2Message.c
    ${_gs}/gt2/gt2Socket.c ${_gs}/gt2/gt2Utility.c
    # HTTP, patching / tracking (the MOTD), stats and persistent storage, CD keys
    ${_gs}/ghttp/ghttpBuffer.c ${_gs}/ghttp/ghttpCallbacks.c ${_gs}/ghttp/ghttpCommon.c
    ${_gs}/ghttp/ghttpConnection.c ${_gs}/ghttp/ghttpEncryption.c ${_gs}/ghttp/ghttpMain.c ${_gs}/ghttp/ghttpPost.c
    ${_gs}/ghttp/ghttpProcess.c
    ${_gs}/pt/ptMain.c
    ${_gs}/gstats/gbucket.c ${_gs}/gstats/gstats.c
    ${_gs}/gcdkey/gcdkeyc.c ${_gs}/gcdkey/gcdkeys.c)
target_include_directories(gamespy_sdk PUBLIC "${THUG2_GAMESPY_DIR}")
target_compile_definitions(gamespy_sdk PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_link_libraries(gamespy_sdk PUBLIC ws2_32)
if(MSVC)
    # (third-party C: its warnings aren't ours to fix)
    target_compile_options(gamespy_sdk PRIVATE /W0 /utf-8)
endif()
