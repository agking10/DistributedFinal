#pragma once

#include "net_include.h"
#include "sp.h"
#include "messages.h"
#include "utils.hpp"

#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <memory>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#define FILE_BLOCK_SIZE 1000
#define MAX_VSSETS 100
#define MAX_UPDATES_BW_SERIALIZE 3

using boost::property_tree::ptree;

void init();
void load_state();
void write_state();
void read_message();
void process_data_message();
void process_membership_message();
void add_log_entry(int, const UserCommand&);
void process_backend_message();
void process_new_email();
void process_read_command();
void process_delete_command();
void send_inbox_to_client();
void send_mail_to_client();
void send_component_to_client();
void process_connection_request();
void process_command_message(bool queue = false);
void apply_new_command(const std::shared_ptr<UserCommand>&);
void apply_command_to_state(const std::shared_ptr<UserCommand>&);
void broadcast_command(const std::shared_ptr<UserCommand>&);
void apply_mail_message(const std::shared_ptr<UserCommand>&);
void apply_read_message(const std::shared_ptr<UserCommand>&);
void apply_delete_message(const std::shared_ptr<UserCommand>&);
void synchronize();
void broadcast_knowledge();
void copy_group_members();
void stash_command();
bool wait_for_everyone();
void clear_synch_arrays();
void update_knowledge();
bool sender_in_group();
void mark_knowledge_as_received();
void send_my_messages();
void count_my_synch_servers();
void send_synch_commands();
void broadcast_messages_from_queue(int, int);
std::list<std::shared_ptr<UserCommand>>::iterator 
    find_message_index(std::list<std::shared_ptr<UserCommand>>&, int);
void apply_queued_updates();
void end_connection(const std::string&);
void goodbye();
void send_ack(uint32_t, const char *);
std::string client_connection_from_id(uint32_t);
std::string client_inbox_from_id(uint32_t);
bool message_sent_to_inbox();
bool message_sent_to_servers();
bool connection_exists(uint32_t);
bool is_server_memb_mess();

void read_state_file();
void read_log_files();

std::multiset<InboxMessage>::iterator
find_mail_by_id(const MessageIdentifier&, const std::string&);

void write_command_to_log(const std::shared_ptr<UserCommand>&);
std::string serialize_command(const std::shared_ptr<UserCommand>&);
std::shared_ptr<UserCommand> deserialize_command(const char *);
std::string get_log_name(int, int, int);

ptree ptree_from_identifier(const MessageIdentifier&);
ptree inbox_to_ptree(const std::pair<std::string, std::multiset<InboxMessage>>&);
ptree ptree_from_inbox_message(const InboxMessage&);
MessageIdentifier identifier_from_ptree(const ptree&);

void extract_inboxes_to_state(const ptree&);
std::multiset<InboxMessage> get_inbox_list_from_ptree(const ptree&);
InboxMessage inbox_message_from_ptree(const ptree&);

struct State
{
    int knowledge[N_MACHINES][N_MACHINES];
    int safe_delivered[N_MACHINES];
    int applied_to_state[N_MACHINES];
    std::unordered_map<std::string, std::multiset<InboxMessage>> inboxes;
    std::set<MessageIdentifier> pending_delete;
    std::set<MessageIdentifier> pending_read;
    std::set<MessageIdentifier> deleted;
    std::set<int> test;
};
