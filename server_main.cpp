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

static std::list<std::shared_ptr<UserCommand>> command_queue[N_MACHINES];

static int n_received;

static bool synchronizing;

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

    init();

    while (true)
    {
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
    if (strcmp(sender, server_group.c_str()) == 0)
    {
        process_backend_message();
    }
    else if (client_connections.find(std::string(sender)) != client_connections.end())
    {
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
    else if (mess_type == MessageType::CONNECT)
    {
        process_connection_request();
    }
}

void process_membership_message()
{
    int ret;
    
    if (!Is_regular_mess(service_type)) return;

    if (strcmp(sender, server_group.c_str()))
    {
        synchronize();
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

    mail_command->id.origin = server_id;
    mail_command->id.index = state.knowledge[server_id][server_id] + 1;

    mail_command->data = *reinterpret_cast<MailMessage*>(mess);
    mail_command->timestamp = time(nullptr);

    apply_command(mail_command);
}

void apply_command(std::shared_ptr<UserCommand> command)
{
    if (command->id.index != state.knowledge[server_id][command->id.origin] + 1) return;

    state.knowledge[server_id][command->id.origin]++;
    write_command_to_log(command);

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

void send_inbox_to_client()
{
    //TODO: send user's inbox to sender's group
}

void send_component_to_client()
{
    //TODO: send component to sender's group
}

void process_connection_request()
{
    //TODO: create new connection
}

void synchronize()
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