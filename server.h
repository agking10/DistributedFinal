#pragma once

#include "net_include.h"
#include "sp.h"
#include "messages.h"

#include <list>
#include <set>
#include <string>
#include <unordered_map>

void init();
void load_state();
void write_state();
void add_log_entry(int, const UserCommand&);
void goodbye();

struct State
{
    int network_view[N_MACHINES][N_MACHINES];
    int messages_safe_delivered[N_MACHINES];
    int applied_to_state[N_MACHINES];
    std::unordered_map<int, std::list<InboxMessage>> inboxes;
    std::unordered_map<std::string, int> users;
    std::set<MessageIdentifier> pending_delete;
    std::set<MessageIdentifier> pending_read;
    std::set<MessageIdentifier> deleted;
};
