#include "server.h"

#include <iostream>
#include <string>
#include <unordered_set>
#include <ctime>

static mailbox mbox;
static int seq_num;
static char user[80];
static char spread_name[80];
static char private_group[MAX_GROUP_NAME];
static char mess[MAX_MESS_LEN];
static char sender[MAX_GROUP_NAME];
static char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
static int n_connected;
static int service_type;
static int16_t mess_type;
static membership_info  memb_info;
static int endian_mismatch;
static sp_time test_timeout;

static std::list<UserCommand> synch_queue;
static std::unordered_set<std::string> client_connections;
static std::unordered_set<int> clients;
static std::string server_group = "all_servers_group";
static std::string server_inbox;
static int server_id;
static int server_index;

static std::list<std::shared_ptr<UserCommand>> command_queue[N_MACHINES];

static int n_received;
static bool synchronizing = false;

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

    load_state();

    sprintf(spread_name, std::to_string(PORT).c_str());

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

void load_state()
{
    //TODO: fetch state from file
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
            case (MessageType::SHOW_COMPONENT):
                send_component_to_client();
                break;
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
    
    if (!Is_reg_memb_mess(service_type)) return;

    ret = SP_get_memb_info(mess, service_type, &memb_info);
    if (ret < 0) 
    {
        printf("BUG: membership message does not have valid body\n");
        SP_error( ret );
        exit( 1 );
    }

    if (!synchronizing && is_server_memb_mess())
    {
        synchronize();
        //garbage_collection();
        //apply_queued_updates();
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
    //TODO: handle messages between servers, during synch or otherwise
}

void process_new_email()
{
    std::shared_ptr<UserCommand> mail_command = std::make_shared<UserCommand>();

    mail_command->id.origin = server_index;
    mail_command->id.index = state.knowledge[server_index][server_index] + 1;

    mail_command->data = *reinterpret_cast<MailMessage*>(mess);
    mail_command->timestamp = time(nullptr);

    apply_command(mail_command);
}

void apply_command(std::shared_ptr<UserCommand> command)
{
    if (command->id.index != state.knowledge[server_index][command->id.origin] + 1) return;

    state.knowledge[server_index][command->id.origin]++;
    write_command_to_log(command);
    
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

    broadcast_command(command);
}

void apply_mail_message(std::shared_ptr<UserCommand> command)
{
    const MailMessage& msg = std::get<MailMessage>(command->data);

    if (state.pending_delete.find(command->id) != state.pending_delete.end())
    {
        state.deleted.insert(command->id);
        state.pending_delete.erase(command->id);
        return;
    }

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

    state.inboxes[std::string(new_mail.msg.to)].push_back(new_mail);
}

void apply_read_message(std::shared_ptr<UserCommand> command)
{
    //TODO
}

void apply_delete_message(std::shared_ptr<UserCommand> command)
{

}

void write_command_to_log(std::shared_ptr<UserCommand> command)
{
    //TODO
}

void process_read_command()
{
    //TODO: apply to state and send to other servers
}

void process_delete_command()
{
    //TODO: apply to state and send to other servers
}

void broadcast_command(std::shared_ptr<UserCommand> command)
{
    SP_multicast(mbox, AGREED_MESS, server_group.c_str(),
        MessageType::COMMAND, sizeof(*command),
        reinterpret_cast<const char *>(&(*command)));
}

void send_inbox_to_client()
{
    GetInboxMessage *msg = reinterpret_cast<GetInboxMessage*>(mess);
    std::string uname = msg->username;
    //TODO: send user's inbox to sender's group
    std::string client_name = client_inbox_from_id(msg->session_id);
    ServerResponse res;
    res.seq_num = msg->seq_num;
    for (const auto& i: state.inboxes[uname]) {
        res.data = i;
        SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
        MessageType::INBOX, sizeof(res), 
        reinterpret_cast<const char *>(&res));
    }
    // AckMessage ack;
    // strcpy(ack.body, "done");
    // res.data = ack;
    // SP_multicast(mbox, AGREED_MESS, client_name.c_str(),
    // MessageType::ACK, sizeof(res), 
    // reinterpret_cast<const char *>(&res));   
    send_ack(msg->session_id, "done");
}

void send_component_to_client()
{
    //TODO: send component to sender's group
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
    //TODO
    bool received[n_connected];
    n_received = 0;

    while (n_received < n_connected)
    {
        read_message();

        if (Is_regular_mess(service_type))
        {
            switch (mess_type)
            {
                case MessageType::KNOWLEDGE:
                    //TODO
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

void stash_command()
{
    //TODO
}

void send_my_messages()
{
    //TODO
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