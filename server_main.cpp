#include "server.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>
#include <limits.h>
#include <ctime>
#include <chrono>

static mailbox mbox;
static int seq_num;
static char user[80];
static char spread_name[80];
static char private_group[MAX_GROUP_NAME];
static char mess[MAX_MESS_LEN];
static char backend_mess[MAX_MESS_LEN];
static char sender[MAX_GROUP_NAME];
static char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
static int n_connected;
static int service_type;
static int16_t mess_type;
static membership_info  memb_info;
static int endian_mismatch;
static sp_time test_timeout;
static int updates_since_serialize = 0;
static int changes_since_garbage_collection = 0;

static std::list<std::shared_ptr<UserCommand>> synch_queue;
static std::unordered_set<std::string> client_connections;
static std::unordered_set<int> clients;
static std::string server_group = "all_servers_group";
static std::string server_inbox;
static int server_id;
static int server_index;
static std::ofstream outfile;

static std::list<std::shared_ptr<UserCommand>> command_queue[N_MACHINES];

// Used for synchronizing
static int n_received;
static int n_synching;
static bool received[N_MACHINES];
static char synch_members[MAX_MEMBERS][MAX_GROUP_NAME];
static bool servers_present[N_MACHINES];
static std::vector<int> current_block(N_MACHINES, 0);
vs_set_info vssets[MAX_VSSETS];
int num_members_;
char members[5][MAX_GROUP_NAME];
static bool need_to_send[N_MACHINES];       // Keep track of which servers' 
                                            //messages we need to send during synch
static int start_index[N_MACHINES];         // Keep track of the first message
                                            // we need to send for servers in need_to_send
static bool synchronizing = false;

static std::string state_file;
static std::string log_state_file;
static std::string inbox_state_file;

static State state;

int main(int argc, char * argv[])
{
    int ret;

    if (argc != 2)
    {
        printf("Usage: ./server <id>\n");
        exit(1);
    }

    server_id = std::stoi(argv[1]);
    server_inbox = "server_" + std::string(argv[1]) + "_in";

    server_index = server_id - 1;

    init();

    while (true)
    {
        read_message();

        if (Is_regular_mess(service_type))
        {
            process_data_message();
        }
        else if (Is_membership_mess(service_type))
        {
            process_membership_message();
        }
    }

    return 0;
}

void read_message()
{
    int ret;
    ret = SP_receive(mbox, &service_type, sender, 100, &n_connected,
            target_groups, &mess_type, 
            &endian_mismatch, sizeof(mess), mess);

    if (ret < 0)
    {
        if ((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT))
        {
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive( mbox, &service_type, sender, MAX_MEMBERS, 
                &n_connected, target_groups, 
                &mess_type, &endian_mismatch, sizeof(mess), mess );
        }
    }
    if (ret < 0) SP_error(ret);
}

void init()
{
    int ret;

    state_file = "state_" + std::to_string(server_id) + ".json";
    log_state_file = "log_" + state_file;
    inbox_state_file = "inbox_" + state_file;

    load_state();

    sprintf(spread_name, std::to_string(PORT).c_str());
    sprintf(user, std::to_string(server_index).c_str());

    test_timeout.sec = 5;
    test_timeout.usec = 0;

    ret = SP_connect_timeout(spread_name, user, 0, 1, &mbox,
        private_group, test_timeout);
    
    if (ret != ACCEPT_SESSION)
    {
        SP_error(ret);
        goodbye();
    }

    printf("User connected to %s with private group %s\n", 
        spread_name, private_group);

    SP_join(mbox, server_inbox.c_str());
    if (ret < 0)
    {
        SP_error(ret);
        goodbye();
    }

    SP_join(mbox, server_group.c_str());
    if (ret < 0)
    {
        SP_error(ret);
        goodbye();
    }
}

void process_data_message()
{
    if (message_sent_to_servers())
    {
        process_backend_message();
    }
    else if (message_sent_to_inbox())
    {
        ClientMessage * msg = reinterpret_cast<ClientMessage*>(mess);
        if (!connection_exists(msg->session_id))
        {
            if (mess_type == MessageType::CONNECT)
            {
                process_connection_request();
            }
            else
            {
                send_ack(msg->session_id, "Must establish a connection before" 
                    "sending messages to server.");
                return;
            }
        }

        // Respond to existing connections
        switch (mess_type)
        {
            case (MessageType::MAIL):
                process_new_email();
                break;
            case (MessageType::READ):
                process_read_command();
                break;
            case (MessageType::DELETE):
                process_delete_command();
                break;
            case (MessageType::SHOW_INBOX):
                send_inbox_to_client();
                break;
            case (MessageType::SHOW_COMPONENT): {
                send_component_to_client();
                break;
            }
            default:
                // Error check here?
                break;
        }
    }
}

void send_ack(uint32_t session_id, const char * msg)
{
    ServerResponse res;
    std::string client_name = client_connection_from_id(session_id);

    AckMessage ack;
    strcpy(ack.body, msg);
    res.data = ack;
    SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
    MessageType::ACK, sizeof(res), 
    reinterpret_cast<const char *>(&res));   
}

void process_membership_message()
{
    int ret;
    // http://www.spread.org/docs/spread_docs_4/docs/user.c following this example still bugged
    if (!Is_reg_memb_mess(service_type)) return;

    ret = SP_get_memb_info(mess, service_type, &memb_info);
    if (ret < 0) 
    {
        printf("BUG: membership message does not have valid body\n");
        SP_error( ret );
        exit( 1 );
    }

    if (!synchronizing && is_server_memb_mess()) //do we chance to synch no matter what?
    {
        synchronize();
        apply_queued_updates();
    }
    else if (client_connections.find(std::string(sender)) != client_connections.end())
    {
        if (n_connected != 2)
        {
            end_connection(std::string(sender));
        }
    }
}

void process_backend_message()
{
    switch (mess_type)
    {
        case MessageType::KNOWLEDGE:
            update_knowledge();
            break;
        case MessageType::COMMAND:
            process_command_message();
            break;
        default:
            break;
    }
}

void process_command_message(bool queue)
{
    UserCommand* msg = reinterpret_cast<UserCommand*>(mess);
    std::shared_ptr<UserCommand> command 
        = std::make_shared<UserCommand>(*msg);
    if (queue)
    {
        synch_queue.push_back(command);
    }
    else
    {
        apply_new_command(command);
    }
}

void process_new_email()
{
    std::shared_ptr<UserCommand> mail_command = std::make_shared<UserCommand>();

    mail_command->id.origin = server_index;
    mail_command->id.index = state.knowledge[server_index][server_index] + 1;

    mail_command->data = *reinterpret_cast<MailMessage*>(mess);
    auto temptime = std::chrono::system_clock::now();
    mail_command->timestamp = std::chrono::system_clock::to_time_t(temptime);
    printf("setting time to: %s\n", std::to_string(std::chrono::system_clock::to_time_t(temptime)).c_str());
    apply_new_command(mail_command);
}

void process_read_command()
{
    send_mail_to_client();
    std::shared_ptr<UserCommand> read_command = std::make_shared<UserCommand>();

    read_command->id.origin = server_index;
    read_command->id.index = state.knowledge[server_index][server_index] + 1;

    read_command->data = *reinterpret_cast<ReadMessage*>(mess);
    auto temptime = std::chrono::system_clock::now();
    read_command->timestamp = std::chrono::system_clock::to_time_t(temptime);

    apply_new_command(read_command);
}

void process_delete_command()
{
    std::shared_ptr<UserCommand> delete_command = std::make_shared<UserCommand>();

    delete_command->id.origin = server_index;
    delete_command->id.index = state.knowledge[server_index][server_index] + 1;

    delete_command->data = *reinterpret_cast<DeleteMessage*>(mess);
    auto temptime = std::chrono::system_clock::now();
    delete_command->timestamp = std::chrono::system_clock::to_time_t(temptime);

    apply_new_command(delete_command);
}

void apply_new_command(const std::shared_ptr<UserCommand>& command)
{
    if (command->id.index != state.knowledge[server_index][command->id.origin] + 1) return;

    write_command_to_log(command);

    apply_command_to_state(command);

    if (command->id.origin == server_index)
        broadcast_command(command); // Broadcast commands that originate on this server
}

void apply_command_to_state(const std::shared_ptr<UserCommand>& command)
{
    ++state.knowledge[server_index][command->id.origin];
    ++state.applied_to_state[command->id.origin];
    ++updates_since_serialize;

    command_queue[command->id.origin].push_back(command);   

    if (std::holds_alternative<MailMessage>(command->data))
    {
        apply_mail_message(command);
    }
    else if (std::holds_alternative<ReadMessage>(command->data))
    {
        apply_read_message(command);
    }
    else if (std::holds_alternative<DeleteMessage>(command->data))
    {
        apply_delete_message(command);
    }

    if (updates_since_serialize >= MAX_UPDATES_BW_SERIALIZE)
    {
        write_inbox_state();
        updates_since_serialize = 0;
    }
}

void apply_mail_message(const std::shared_ptr<UserCommand>& command)
{
    const MailMessage& msg = std::get<MailMessage>(command->data);

    InboxMessage new_mail;
    strcpy(new_mail.msg.to, msg.to);
    strcpy(new_mail.msg.from, msg.username);
    strcpy(new_mail.msg.subject, msg.subject);
    strcpy(new_mail.msg.message, msg.message);
    new_mail.msg.date_sent = command->timestamp;
    new_mail.id = command->id;
    new_mail.msg.read = false;

    if (state.pending_read.find(command->id) != state.pending_read.end())
    {
        new_mail.msg.read = true;
        state.pending_read.erase(command->id);
    }

    state.inboxes[std::string(new_mail.msg.to)].insert(new_mail);
    
    char temp[100];
    strcpy(temp, "done1 ");
    strcat(temp, std::to_string(state.inboxes[std::string(new_mail.msg.to)].size()).c_str());
    send_ack(msg.session_id, temp);
}

void apply_read_message(const std::shared_ptr<UserCommand>& command)
{
    const ReadMessage& msg = std::get<ReadMessage>(command->data);
    bool exist = false;
    auto it = find_mail_by_id(msg.id, msg.username);
    if (it != state.inboxes[msg.username].end())
    {
        exist = true;
        InboxMessage new_msg = *it;
        state.inboxes[msg.username].erase(*it);
        new_msg.msg.read = true;
        state.inboxes[msg.username].insert(new_msg);
    }
    if (!exist) {
        state.pending_read.insert(msg.id);
    }
    send_ack(msg.session_id, "done\n");
}

void apply_delete_message(const std::shared_ptr<UserCommand>& command)
{
    const DeleteMessage& msg = std::get<DeleteMessage>(command->data);
    bool exist = false;

    auto it = find_mail_by_id(msg.id, msg.username);
    if (it != state.inboxes[msg.username].end())
        exist = true;
    
    if (!exist) {
        state.pending_delete.insert(msg.id);
        char temp[100];
        strcpy(temp, "could not find to delete: ");
        strcat(temp, std::to_string(msg.id.origin).c_str());
        strcat(temp, std::to_string(msg.id.index).c_str());
        strcat(temp, "first was: ");
        strcat(temp, std::to_string((*(state.inboxes[msg.username].begin())).id.origin).c_str());
        strcat(temp, std::to_string((*(state.inboxes[msg.username].begin())).id.index).c_str());
        send_ack(msg.session_id, temp);
    } else {
        state.inboxes[msg.username].erase(*it);
        char temp[100];
        strcpy(temp, "success_delete ");
        strcat(temp, std::to_string(state.inboxes[std::string(msg.username)].size()).c_str());
        send_ack(msg.session_id, temp);
    }
}

void broadcast_command(const std::shared_ptr<UserCommand>& command)
{
    SP_multicast(mbox, AGREED_MESS, server_group.c_str(),
        MessageType::COMMAND, sizeof(*command),
        reinterpret_cast<const char *>(&(*command)));
}

void send_inbox_to_client()
{
    //TODO consolidate all emails before sending (slack message)
    GetInboxMessage *msg = reinterpret_cast<GetInboxMessage*>(mess);
    std::string uname = msg->username;
    std::string client_name = client_inbox_from_id(msg->session_id);
    
    ServerInboxResponse res;
    int counter = 0;
    for (const auto& i: state.inboxes[uname]) {
        strcpy(res.inbox[counter].subject, i.msg.subject);
        strcpy(res.inbox[counter].sender, i.msg.from);
        res.inbox[counter].read = i.msg.read;
        res.inbox[counter].id.index = i.id.index;
        res.inbox[counter].id.origin = i.id.origin;
        res.inbox[counter].timestamp = i.msg.date_sent;
        if (counter >= INBOX_LIMIT) break;
        counter++;
    }
    res.mail_count = counter;
    SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
    MessageType::INBOX, sizeof(res), 
    reinterpret_cast<const char *>(&res));

    char temp[100];
    strcpy(temp, "done ");
    strcat(temp, std::to_string(state.inboxes[uname].size()).c_str());
    send_ack(msg->session_id, temp);
}

void send_mail_to_client()
{
    //TODO consolidate all emails before sending (slack message)
    ReadMessage *msg = reinterpret_cast<ReadMessage*>(mess);
    std::string uname = msg->username;
    std::string client_name = client_inbox_from_id(msg->session_id);
    
    ServerResponse res;
    bool exist = false;
    for (const auto& i: state.inboxes[uname]) {
        if (i.id == msg->id) {
            res.data = i;
            SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
            MessageType::RESPONSE, sizeof(res), 
            reinterpret_cast<const char *>(&res));
            exist = true;
        }
    }
    if (!exist) {
        char temp[100];
        strcpy(temp, "couldnt find ");
        strcat(temp, std::to_string((*(state.inboxes[uname].begin())).id.origin).c_str());
        strcat(temp, std::to_string((*(state.inboxes[uname].begin())).id.index).c_str());
        send_ack(msg->session_id, temp);
            }
}

void send_component_to_client()
{
    GetComponentMessage *msg = reinterpret_cast<GetComponentMessage*>(mess);
    std::string client_name = client_inbox_from_id(msg->session_id);

    ServerResponse res;
    ComponentMessage comp;
    comp.num_servers = n_synching;
    int index = 0;
    for (int j = 0; j < 5; j++) {
        if (servers_present[j]) {
            strcpy(comp.names[index], synch_members[j]);
            index++;
        }
    }
    res.data = comp;
    SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
            MessageType::COMPONENT, sizeof(res), 
            reinterpret_cast<const char *>(&res));
    char temp [100];
    strcpy(temp, "total num of servers: ");
    strcat(temp, std::to_string(comp.num_servers).c_str());
    send_ack(msg->session_id, temp);
    printf("hereeee \n");
}

void process_connection_request()
{
    ConnectMessage * msg = reinterpret_cast<ConnectMessage*>(mess);
    std::string conn_group = "client_" + std::to_string(msg->session_id) + "_connect";
    SP_join(mbox, conn_group.c_str());
    client_connections.insert(conn_group);
}

void synchronize()
{
    synchronizing = true;
    start:
    copy_group_members();
    broadcast_knowledge();

    // Check if another split occurred
    if (!wait_for_everyone())
        goto start;

    send_my_messages();
    synchronizing = false;
}

void broadcast_knowledge()
{
    KnowledgeMessage msg;
    msg.sender = server_index;
    for (int i = 0; i < N_MACHINES; i++)
    {
        for (int j = 0; j < N_MACHINES; j++)
        {
            msg.summary[i][j] = state.knowledge[i][j];
        }
    }

    SP_multicast(mbox, AGREED_MESS, server_group.c_str(),
        MessageType::KNOWLEDGE, sizeof(msg),
        reinterpret_cast<const char *>(&msg));
}

void copy_group_members()
{
    for (int i = 0; i < n_connected; i++)
    {
        strcpy(synch_members[i], target_groups[i]);
        printf("copying: ");
        printf("%s\n", target_groups[i]);
    }
}

/*
    Receives a knowledge message from every server in the group except us.
    If a serviceable command is received or connection is established, handle 
    it immediately.
    If an update command is received, stash it in the queue.
    If a membership message is received from server_group while waiting, 
    return false.
*/
bool wait_for_everyone()
{
    //TODO:
    clear_synch_arrays();

    n_received = 1;
    received[server_index] = true;
    servers_present[server_index] = true;
    n_synching = n_connected;

    while (n_received < n_synching)
    {
        read_message();

        if (Is_regular_mess(service_type))
        {
            switch (mess_type)
            {
                case MessageType::KNOWLEDGE:
                    update_knowledge();
                    mark_knowledge_as_received();
                    break;
                case MessageType::CONNECT:
                    process_connection_request();
                    break;
                case MessageType::SHOW_INBOX:
                    send_inbox_to_client();
                    break;
                case MessageType::SHOW_COMPONENT:
                    send_component_to_client();
                    break;
                case MessageType::COMMAND:
                    process_command_message(true);
                    break;
                case MessageType::MAIL:
                case MessageType::READ:
                case MessageType::DELETE:
                    stash_command();
                    break;
                default:
                    break;
            }
        }
        else if (Is_reg_memb_mess(service_type))
        {
            if (is_server_memb_mess()) return false;

            process_membership_message();
        }
    }
    return true;
}

void clear_synch_arrays()
{
    for (int i = 0; i < N_MACHINES; i++)
    {
        received[i] = false;
        servers_present[i] = false;
    }
}

void update_knowledge()
{
    KnowledgeMessage * msg = reinterpret_cast<KnowledgeMessage*>(mess);

    for (int i = 0; i < N_MACHINES; i++)
    {
        for (int j = 0; j < N_MACHINES; j++)
        {
            if (msg->summary[i][j] > state.knowledge[i][j])
            {
                state.knowledge[i][j] = msg->summary[i][j];
                ++changes_since_garbage_collection;
            }
        }
    }

    ++changes_since_garbage_collection;
    if (changes_since_garbage_collection >= MAX_CHANGES_BW_GARBAGE)
    {
        collect_garbage();
        changes_since_garbage_collection = 0;
    }
}

void mark_knowledge_as_received()
{
    //TODO: make sure group_id is correct
    KnowledgeMessage * msg = reinterpret_cast<KnowledgeMessage*>(mess);

    // Need to check that this member is currently in our partition
    // and we haven't received an update from them yet
    if (sender_in_group() && received[msg->sender] == false)
    {
        received[msg->sender] = true;
        servers_present[msg->sender] = true;
        ++n_received;
    }
}

void collect_garbage()
{
    for (int i = 0; i < N_MACHINES; ++i)
    {
        int min_index = INT_MAX;
        for (int j = 0; j < N_MACHINES; ++j)
        {
            min_index = std::min(min_index, state.knowledge[j][i]);
        }
        state.safe_delivered[i] = min_index;
        erase_queue_up_to(i, min_index);
    }
}

void erase_queue_up_to(int origin, int index)
{
    auto& queue = command_queue[origin];
    while (!queue.empty() && queue.front()->id.index <= index)
    {
        if (different_block(origin, queue.front()->id.index))
        {
            write_log_state();
            for (int i = current_block[origin]; 
                i < queue.front()->id.index / FILE_BLOCK_SIZE; i++)
            {
                delete_file_block(origin, i);
            }
            current_block[origin] = index / FILE_BLOCK_SIZE;
        }
        queue.pop_front();
    }
}

bool different_block(int origin, int index)
{
    return current_block[origin] != index / FILE_BLOCK_SIZE;
}

void delete_file_block(int origin, int block)
{
    int index = block * FILE_BLOCK_SIZE;
    delete_file_block_by_index(origin, index);
}

void delete_file_block_by_index(int origin, int index)
{
    const std::string filename = get_log_name(origin, index);
    if (std::filesystem::exists(filename.c_str()));
        remove(filename.c_str());
}

bool sender_in_group()
{
    for (int i = 0; i < n_synching; i++)
    {
        if (strcmp(sender, synch_members[i]) == 0)
            return true;
    }
    return false;
}

void stash_command()
{
    std::shared_ptr<UserCommand> new_command = std::make_shared<UserCommand>();
    new_command->id.origin = server_index;
    new_command->id.index = state.knowledge[server_index][server_index] + 1;
    auto temptime = std::chrono::system_clock::now();
    new_command->timestamp = std::chrono::system_clock::to_time_t(temptime);
    switch(mess_type) {
        case MessageType::MAIL:
            new_command->data = *reinterpret_cast<MailMessage*>(mess);
            break;
        case MessageType::READ:
            new_command->data = *reinterpret_cast<ReadMessage*>(mess);
            break;
        case MessageType::DELETE: 
            new_command->data = *reinterpret_cast<DeleteMessage*>(mess);
            break;
    }
    synch_queue.push_back(new_command);
}

void send_my_messages()
{
    count_my_synch_servers();
    send_synch_commands();
}

/*
    Records which servers' messages we are most up to date on and therefore
    need to send to the group. Ties are broken by server id.
*/
void count_my_synch_servers()
{
    //TODO: MAKE SURE COLUMN AND ROW ORDER ARE CORRECT
    for (int i = 0; i < N_MACHINES; i++)
    {
        need_to_send[i] = false;
    }

    for (int i = 0; i < N_MACHINES; i++) // Loop over each origin server
    {
        int max_server = 0;
        int max_message = INT_MIN;
        int min_message = INT_MAX;
        for (int j = 0; j < N_MACHINES; j++) // Loop over servers in this group
        {
            if (!servers_present[j]) continue; // Skip if this server is not in the group
            if (state.knowledge[j][i] > max_message)
            {
                max_server = j;
                max_message = state.knowledge[j][i];
            }
            if (state.knowledge[j][i] < min_message)
            {
                min_message = state.knowledge[j][i];
            }
        }
        if (max_server == server_index)
        {
            need_to_send[i] = true;
            start_index[i] = min_message;
        }
    }
}

void send_synch_commands()
{
    for (int i = 0; i < N_MACHINES; i++)
    {
        if (need_to_send[i])
        {
            broadcast_messages_from_queue(i, start_index[i]);
        }
    }
}

void broadcast_messages_from_queue(int origin, int start_index)
{
    auto it = find_message_index(command_queue[origin], start_index);
    while (it != command_queue[origin].end())
    {
        broadcast_command(*it);
        ++it;
    }
}

/*
    Linear scan through list to find user command with index start_index.
    If not found, return list.cend();
*/
std::list<std::shared_ptr<UserCommand>>::iterator 
find_message_index(std::list<std::shared_ptr<UserCommand>>& queue, int start_index)
{
    auto it = queue.begin();
    while (it != queue.end())
    {
        if ((*it)->id.index == start_index + 1) return it;
        ++it;
    }
    return it;
}

void apply_queued_updates()
{
    while (!synch_queue.empty())
    {
        apply_new_command(synch_queue.front());
        synch_queue.pop_front();
    }
}

void end_connection(const std::string& group)
{
    SP_leave(mbox, group.c_str());
    client_connections.erase(group);
}

void goodbye()
{
    printf("Bye.\n");

    SP_disconnect(mbox);

    exit(0);
}

std::string client_connection_from_id(uint32_t id)
{
    return "client_" + std::to_string(id) + "_connect";
}

std::string client_inbox_from_id(uint32_t id)
{
    return "client_" + std::to_string(id) + "_in";
}

bool message_sent_to_inbox()
{
    return strcmp(target_groups[0], server_inbox.c_str()) == 0;
}

bool message_sent_to_servers()
{
    return strcmp(target_groups[0], server_group.c_str()) == 0;
}

bool connection_exists(uint32_t session_id)
{
    return client_connections.find(client_connection_from_id(session_id))
        != client_connections.end();
}

bool is_server_memb_mess()
{
    return strcmp(sender, server_group.c_str()) == 0;
}

std::multiset<InboxMessage>::iterator
find_mail_by_id(const MessageIdentifier& id, const std::string& name)
{
    for (auto it = state.inboxes[name].begin(); it != state.inboxes[name].end(); it++)
    {
        if (*it == id)
        {
            return it;
        }
    }
    return state.inboxes[name].end();
}

void load_state()
{
    read_state_file();

    read_log_files();
}

void read_state_file()
{
    require(
        std::filesystem::exists(inbox_state_file),
        "No inbox state file detected",
        read_inbox_state
    );
    require(
        std::filesystem::exists(log_state_file),
        "No log state file detected",
        read_log_state
    );
    repopulate_local_data();
}

void read_inbox_state()
{
    ptree state_tree;
    std::string filename = inbox_state_file;

    read_json(inbox_state_file, state_tree);
    read_2d_ptree_array(state.knowledge, N_MACHINES, N_MACHINES, 
        state_tree.get_child("knowledge"));
    read_1d_ptree_array(state.applied_to_state, N_MACHINES, 
        state_tree.get_child("applied_to_state"));
    rehydrate_set_from_ptree(state.pending_read, identifier_from_ptree, 
        state_tree.get_child("pending_read"));
    rehydrate_set_from_ptree(state.pending_delete, identifier_from_ptree, 
        state_tree.get_child("pending_delete"));
    extract_inboxes_to_state(state_tree.get_child("inboxes"));
}

void read_log_state()
{
    ptree state_tree;
    std::string filename = log_state_file;
    read_1d_ptree_array(state.safe_delivered, N_MACHINES, 
        state_tree.get_child("safe_delivered"));
}

void repopulate_local_data()
{
    for (int i = 0; i < N_MACHINES; ++i)
    {
        current_block[i] = (state.safe_delivered[i] + 1) / FILE_BLOCK_SIZE;
    }
}

void extract_inboxes_to_state(const ptree& pt)
{
    for (const auto& inbox : pt.get_child(""))
    {
        state.inboxes[inbox.first] = get_inbox_list_from_ptree(inbox.second);
    }
}

std::multiset<InboxMessage> get_inbox_list_from_ptree(const ptree& pt)
{
    std::multiset<InboxMessage> inbox;
    for (const auto& child : pt)
    {
        inbox.insert(inbox_message_from_ptree(child.second));
    }
    return inbox;
}

void read_log_files()
{
    int index;
    int current_block;
    std::shared_ptr<UserCommand> command;
    UserCommand buf;
    std::string filename;
    std::ifstream infile;
    std::string line;
    for (int i = 0; i < N_MACHINES; i++)
    {
        index = state.applied_to_state[i] + 1;
        current_block = index / FILE_BLOCK_SIZE;

        filename = get_log_name(i, index);
        if (!std::filesystem::exists(filename)) continue;
        infile.open(filename, std::ios::binary);

        // Apply all remaining files to state
        while (!infile.eof())
        {
            infile.read(reinterpret_cast<char*>(&buf), sizeof(UserCommand));
            if (buf.id.index == index) 
            {
                apply_command_to_state(std::make_shared<UserCommand>(buf));
                ++index;
            }

            if (index / FILE_BLOCK_SIZE != current_block)
            {
                infile.close();
                if (std::filesystem::exists(get_log_name(i, index)))
                {
                    current_block = index / FILE_BLOCK_SIZE;
                    infile.open(get_log_name(i, index), std::ios::binary);
                }
                else
                {
                    break;
                }
            }
        }
        if (infile.is_open()) infile.close();
    }
}

void write_command_to_log(const std::shared_ptr<UserCommand>& command)
{
    const int origin = command->id.origin;
    const int index = command->id.index;

    int block = index / FILE_BLOCK_SIZE;

    outfile.open(get_log_name(origin, index), 
        std::ios_base::app | std::ios::binary);

    outfile.write(reinterpret_cast<const char*>(&(*command)), sizeof(UserCommand));
    outfile.close();
}

void write_state()
{
    write_inbox_state();
    write_log_state();
}

void write_inbox_state()
{
    ptree state_tree;
    state_tree.push_back(std::make_pair("knowledge", 
        generate_2d_ptree(state.knowledge, N_MACHINES, N_MACHINES)));
    state_tree.push_back(std::make_pair("applied_to_state",
        generate_1d_ptree(state.applied_to_state, N_MACHINES)));

    state_tree.push_back(std::make_pair("pending_delete",
        generate_iterable_ptree(state.pending_delete, ptree_from_identifier)));
    state_tree.push_back(std::make_pair("pending_read",
        generate_iterable_ptree(state.pending_read, ptree_from_identifier)));

    state_tree.push_back(std::make_pair("inboxes", write_inboxes_to_ptree()));

    write_json(inbox_state_file, state_tree);
}

ptree write_inboxes_to_ptree()
{
    ptree inbox_tree;
    for (const auto& inbox : state.inboxes)
    {
        inbox_tree.push_back(std::make_pair(inbox.first, 
            ptree_from_inbox(inbox.second)));
    }
    return inbox_tree;
}

ptree ptree_from_inbox(const std::multiset<InboxMessage>& inbox)
{
    ptree inbox_tree;
    for (const auto& message : inbox)
    {
        inbox_tree.push_back(std::make_pair("", ptree_from_inbox_message(message)));
    }
    return inbox_tree;
}

std::string get_log_name(int origin, int index)
{
    return "log_" 
    + std::to_string(server_id) 
    + "_" + std::to_string(origin)
    + "_" + std::to_string(index / FILE_BLOCK_SIZE);
}

ptree ptree_from_identifier(const MessageIdentifier& id)
{
    ptree output;
    output.put("origin", id.origin);
    output.put("index", id.index);
    return output;
}

MessageIdentifier identifier_from_ptree(const ptree& pt)
{
    MessageIdentifier id;
    id.origin = pt.get_child("origin").get_value<int>();
    id.index = pt.get_child("index").get_value<int>();
    return id;
}

ptree inbox_to_ptree(const std::pair<std::string, std::multiset<InboxMessage>>& inbox)
{
    ptree output;
    output.push_back(std::make_pair(inbox.first, 
        generate_iterable_ptree(inbox.second, ptree_from_inbox_message)));
    return output;
}

ptree ptree_from_inbox_message(const InboxMessage& message)
{
    ptree output;
    output.put("read", message.msg.read);
    output.push_back(std::make_pair("id", ptree_from_identifier(message.id)));
    output.put("date_sent", message.msg.date_sent);
    output.put("to", message.msg.to);
    output.put("from", message.msg.from);
    output.put("subject", message.msg.subject);
    output.put("message", message.msg.message);
    return output;
}

InboxMessage inbox_message_from_ptree(const ptree& pt)
{
    InboxMessage result;
    result.id = identifier_from_ptree(pt.get_child("id"));
    result.msg.date_sent = pt.get<time_t>("date_sent");
    strcpy(result.msg.from, pt.get<std::string>("from").c_str());
    strcpy(result.msg.to, pt.get<std::string>("to").c_str());
    strcpy(result.msg.subject, pt.get<std::string>("subject").c_str());
    strcpy(result.msg.message, pt.get<std::string>("message").c_str());
    result.msg.read = pt.get<bool>("read");
    return result;
}

void write_log_state()
{
    ptree state_tree;
    state_tree.push_back(std::make_pair("safe_delivered",
        generate_1d_ptree(state.safe_delivered, N_MACHINES)));
    
    write_json(log_state_file, state_tree);
}

