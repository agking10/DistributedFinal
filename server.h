#pragma once

#include "net_include.h"
#include "sp.h"
#include "messages.h"

#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <memory>

void init();
void load_state();
void write_state();
void process_data_message();
void process_membership_message();
void add_log_entry(int, const UserCommand&);
void process_backend_message();
void process_new_email();
void process_read_command();
void process_delete_command();
void send_inbox_to_client();
void send_component_to_client();
void process_connection_request();
void apply_command(std::shared_ptr<UserCommand>);
void apply_mail_message(std::shared_ptr<UserCommand>);
void apply_read_message(std::shared_ptr<UserCommand>);
void apply_delete_message(std::shared_ptr<UserCommand>);
void synchronize();
void end_connection(const std::string&);
void write_command_to_log(std::shared_ptr<UserCommand>);
void goodbye();

struct State
{
    int knowledge[N_MACHINES][N_MACHINES];
    int messages_safe_delivered[N_MACHINES];
    int applied_to_state[N_MACHINES];
    std::unordered_map<std::string, std::list<InboxMessage>> inboxes;
    std::set<MessageIdentifier> pending_delete;
    std::set<MessageIdentifier> pending_read;
    std::set<MessageIdentifier> deleted;
};
