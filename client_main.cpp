#include "client.h"
#include "messages.h"

#include <string>
#include "utils.hpp"
#include <stdint.h>
#include <stdexcept>
#include <unordered_map>
#include <list>
#include <set>
#include <cstdlib>

static std::string username;
//static int uid;
static std::string connection_group;
static int connected_server_id;
static std::string connected_server_inbox;
static std::string client_inbox;
static bool logged_in;
static bool connected;
static uint32_t session_id;
static sp_time test_timeout;

//Spread structs
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

// Keep track of requests that have not been filled yet
static std::unordered_map<int, std::list<ServerResponse>> requests;
static std::multiset<InboxHeader> inbox;
static bool blocking;
static int blocking_id;
static bool listed = false;

int main(int argc, char * argv[])
{
    start();

    E_init();

    E_attach_fd(0, READ_FD, handle_keyboard_in, 0, NULL, LOW_PRIORITY);

    E_attach_fd(mbox, READ_FD, handle_spread_message, 0, NULL, HIGH_PRIORITY);

    seq_num = 0;

    printf("User> ");
    fflush(stdout);

    E_handle_events();

    return 0;
}


void start()
{
    int ret;

    sprintf(spread_name, std::to_string(PORT).c_str());

    srand(time(nullptr));
    session_id = rand();

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
}

void handle_keyboard_in(int, int, void*)
{
    char command[130] = {0};
    char args[100] = {0};
    int ret;

    if (fgets(command, 130, stdin) == nullptr)
        goodbye();

    switch (command[0])
    {
        case 'u':
            ret = sscanf(&command[2], "%s", args);
            if (ret < 1)
            {
                printf("Invalid username\n");
                break;
            }
            log_in(args);
            break;
        case 'c':
            ret = sscanf(&command[2], "%s", args);
            if (ret < 1)
            {
                printf("Invalid username\n");
                break;
            }
            try
            {
                require(
                    logged_in,
                    "User is not logged in",
                    connect_to_server,
                    std::stoi(args)
                );
            }
            catch(const std::invalid_argument& e)
            {
                printf("Invalid server name, must be 1-5\n");
            }
            break;
        case 'm':
            require(
                connected,
                "Not connected to a server. "
                "User must be connected to send mail.",
                send_email
            );
            break;
        case 'l':
            require(
                connected,
                "Must be connected to a server to view inbox.",
                get_inbox
            );
            break;
        case 'd':
            ret = sscanf(&command[2], "%s", args);
            if (ret < 1)
            {
                printf("Invalid mail selection\n");
                break;
            }
            try
            {
                require(
                    listed,
                    "Must list mail first",
                    delete_email,
                    std::stoi(args)
                );
            }
            catch(const std::invalid_argument& e)
            {
                printf("Invalid mail selection\n");
            }
            break;
        case 'r':
            ret = sscanf(&command[2], "%s", args);
            if (ret < 1)
            {
                printf("Invalid mail selection\n");
                break;
            }
            try
            {
                require(
                    listed,
                    "Must list mail first",
                    read_email,
                    std::stoi(args)
                );
            }
            catch(const std::invalid_argument& e)
            {
                printf("Invalid mail selection\n");
            }
            break;
        case 'v':
            require(
                connected,
                "Must be connected to a server to view components.",
                get_component
            );
            break;
        case 'h':
            print_menu();
            break;
        case 'q':
            goodbye();
            break;
        default:
            printf("Command not recognized. Type 'h' for list of valid commands.\n");
            break;
    }
    printf("User> ");
    fflush(stdout);
}

void handle_spread_message(int, int, void*)
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

    if (Is_regular_mess(service_type))
    {
        process_server_response(mess_type, mess);
    }
    else if (Is_membership_mess(service_type))
    {
        process_membership_message(sender, mess, service_type, memb_info, n_connected);
    }
}

void process_membership_message(const char * sender, 
    const char * mess, int service_type, membership_info memb_info, int n_connected)
{
    int ret;
    if (strcmp(sender, connection_group.c_str()) != 0) return;

    ret = SP_get_memb_info(mess, service_type, &memb_info);
    if (ret < 0) 
    {
        printf("BUG: membership message does not have valid body\n");
        SP_error( ret );
        exit( 1 );
    }

    if (Is_reg_memb_mess(service_type))
    {
        if (Is_caused_join_mess(service_type))
        {
            if (n_connected == 2)
            {
                connection_success();
            }
        }
        else if (Is_caused_leave_mess(service_type) || Is_caused_disconnect_mess(service_type))
        {
            disconnect();
            printf("Connection with server has closed. Please connect to a different server.\n");
            printf("User> ");
            fflush(stdout);
        }
    }
}

void process_server_response(int16_t mess_type, const char * mess)
{
    if (!blocking) return;
    printf("in process_server_response\n");
    if (mess_type == MessageType::ACK)
    {
        const ServerResponse * resp = reinterpret_cast<const ServerResponse*>(mess);
        AckMessage ack = std::get<AckMessage>(resp->data);
        printf("%s\n", ack.body);
        blocking = false;
    }
    else if (mess_type == MessageType::INBOX)
    {
        const ServerInboxResponse* resp = reinterpret_cast<const ServerInboxResponse*>(mess);
        for (int i = 0; i < resp->mail_count; i++) {
            inbox.insert(resp->inbox[i]);
        }
        int indx = 1;
        for (const auto & i: inbox) {
            printf(std::to_string(indx).c_str());

            printf(". from: %s subject: %s read: %s timestamp: %s\n", i.sender, i.subject, 
                i.read ? "true" : "false", std::to_string(i.timestamp).c_str());
            indx++;
        }
    }
    else if (mess_type == MessageType::RESPONSE) {
        const ServerResponse * resp = reinterpret_cast<const ServerResponse*>(mess);
        InboxMessage msg = std::get<InboxMessage>(resp->data);
        printf("From: %s\n Subject: %s\n%s", msg.msg.from, msg.msg.subject, msg.msg.message);
    } 
    else if (mess_type == MessageType::COMPONENT) {
        const ServerResponse * resp = reinterpret_cast<const ServerResponse*>(mess);
        ComponentMessage msg = std::get<ComponentMessage>(resp->data);
        printf("Components: \n");
        for (int j = 0; j < msg.num_servers; j++) {
            printf("%s\n", msg.names[j]);
        }
    }
}

void connection_success()
{
    connected = true;
    printf("Successfully connected to server %d.\n", connected_server_id);
    printf("User> ");
    fflush(stdout);
}

void log_in(const char * user)
{
    if (logged_in)
        log_out();
    
    username = std::string(user);
    logged_in = true;
}

void log_out()
{
    logged_in = false;
}

void connect_to_server(int server)
{
    int ret;
    if (server < 1 || server > 5)
    {
        printf("Invalid server name, must be 1-5\n");
        return;
    }

    if (connected)
        disconnect();

    ConnectMessage msg;
    msg.session_id = session_id;

    connection_group = "client_" + std::to_string(session_id) + "_connect";
    connected_server_inbox = "server_" + std::to_string(server) + "_in";
    client_inbox = "client_" + std::to_string(session_id) + "_in";
    connected_server_id = server;

    ret = SP_join(mbox, connection_group.c_str());
    if (ret < 0) SP_error(ret);
    ret = SP_join(mbox, client_inbox.c_str());
    if (ret < 0) SP_error(ret);

    //TODO add timeout

    SP_multicast(mbox, AGREED_MESS, 
        connected_server_inbox.c_str(),
        MessageType::CONNECT, sizeof(msg), 
        reinterpret_cast<const char *>(&msg)
    );
}

void connect_failure_handler(int, void*)
{
    leave_current_session();
}

void leave_current_session()
{
    int ret;

    // Disconnect from groups so we can't be contacted anymore
    ret = SP_leave(mbox, connection_group.c_str());
    if (ret < 0) SP_error(ret);
    ret = SP_leave(mbox, client_inbox.c_str());
    if (ret < 0) SP_error(ret);

    // clear request queue
    requests.clear();

    // Generate new session id
    session_id = rand();
}

void disconnect()
{
    leave_current_session();

    connected = false;
}

void send_email()
{
    printf("To: ");
    MailMessage msg;
    if (fgets(msg.to, MAX_USERNAME, stdin) == NULL)
    {
        printf("Invalid send address\n");
        return;
    }
    strip_newline(msg.to);
    printf("Subject: ");
    if (fgets(msg.subject, MAX_SUBJECT, stdin) == NULL)
    {
        printf("Invalid subject\n");
        return;
    }
    strip_newline(msg.subject);
    printf("Message: ");
    if (fgets(msg.message, EMAIL_LEN, stdin) == NULL)
    {
        printf("Invalid message body\n");
        return;
    }
    strip_newline(msg.message);
    msg.seq_num = seq_num++;
    msg.session_id = session_id;
    strcpy(msg.username, username.c_str());


    SP_multicast(mbox, AGREED_MESS, 
        connected_server_inbox.c_str(), 
        MessageType::MAIL,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );
}

void get_inbox()
{
    GetInboxMessage msg;
    msg.seq_num = seq_num++;
    msg.session_id = session_id;
    strcpy(msg.username, username.c_str());

    SP_multicast(mbox, AGREED_MESS,
        connected_server_inbox.c_str(),
        MessageType::SHOW_INBOX,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );

    inbox.clear();

    blocking = true;
    while (blocking && connected)
    {
        handle_spread_message(0, 0, nullptr);
    }
    blocking = false;
    listed = true;
}

void delete_email(int index) {

    DeleteMessage msg;
    msg.session_id = session_id;
    strcpy(msg.username, username.c_str());
    msg.id = find_id_using_index(index - 1);
    
    char temp[100];
    strcpy(temp, "");
    strcat(temp, std::to_string(msg.id.index).c_str());
    strcat(temp, std::to_string(msg.id.origin).c_str());
    printf("id: %s", temp);

    SP_multicast(mbox, AGREED_MESS, 
        connected_server_inbox.c_str(), 
        MessageType::DELETE,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );

    blocking = true;
    while (blocking && connected)
    {
        handle_spread_message(0, 0, nullptr);
    }
    blocking = false;
    listed = true;
}

void read_email(int index) {

    if (index < 1 || index > inbox.size()) {
        printf("invalid selection\n");
        return;
    }
    ReadMessage msg;
    msg.session_id = session_id;
    strcpy(msg.username, username.c_str());
    msg.id = find_id_using_index(index - 1);
    char temp[100];
    strcpy(temp, "");
    strcat(temp, std::to_string(msg.id.index).c_str());
    strcat(temp, std::to_string(msg.id.origin).c_str());
    printf("id: %s", temp);
    
    SP_multicast(mbox, AGREED_MESS, 
        connected_server_inbox.c_str(), 
        MessageType::READ,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );

    blocking = true;
    while (blocking && connected)
    {
        handle_spread_message(0, 0, nullptr);
    }
    blocking = false;
}

void get_component() {
    GetComponentMessage msg;
    msg.session_id = session_id;
    printf("here1\n");
    SP_multicast(mbox, AGREED_MESS,
        connected_server_inbox.c_str(),
        MessageType::SHOW_COMPONENT,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );

    blocking = true;
    while (blocking && connected)
    {
        handle_spread_message(0, 0, nullptr);
    }
    blocking = false;
}

MessageIdentifier find_id_using_index(int index) {
    int counter = 0;
    for (const auto & i: inbox) {
        if (counter == index) {
            MessageIdentifier temp;
            temp.origin = i.id.origin;
            temp.index = i.id.index;
            return temp;
        }
        counter++;
    }
}

void goodbye()
{
    std::cout << "Bye." << std::endl;

    SP_disconnect(mbox);

    exit(0);
}

void print_menu()
{
    printf("\n");
	printf("==========\n");
	printf("User Menu:\n");
	printf("----------\n");
	printf("\n");
	printf("\tu <username> -- log in as <username>\n");
	printf("\tc <server number> -- connect to server <server number>\n");
	printf("\n");
	printf("\tm -- send an email\n");
    printf("\tl -- show current user's inbox\n");
	printf("\tr <i> -- mark the ith message in the inbox as read\n");
	printf("\td <i> -- delete the ith message in the inbox \n");
	printf("\tv -- show servers in current component\n");
	printf("\th -- help menu \n");
	printf("\n");
	printf("\tq -- quit\n");
	fflush(stdout);
}