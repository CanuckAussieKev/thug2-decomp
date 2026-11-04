#pragma once

// NOT ORIGINAL - the online session's answers to the engine's questions (engine/net/session_query.h), installed at
// start-up right after the network manager is made (app/startup.cpp), where the original kept the objects' own copy
// of its pointer (`hub`).
namespace online {

class Hub;

void install_session_queries(Hub* hub);

} // namespace online
