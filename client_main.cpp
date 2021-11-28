#include "client.h"
#include "messages.h"

#include <string>
#include "utils.hpp"
#include <stdint.h>
#include <stdexcept>
#include <random>
#include <unordered_map>
#include <list>
#include <set>

using RNG = std::mt19937;

static std::string username;
//static int uid;
static std::string connection_group;
static std::string connected_server_inbox;
static std::string client_inbox;
static bool logged_in;
static bool connected;
static uint32_t session_id;
static RNG generator;
static sp_time test_timeout;
std::uniform_int_distribution<uint32_t> rng_dst;

//Spread structs
static mailbox mbox;
static int seq_num;
static char user[80];
static char spread_name[80];
static char private_group[MAX_GROUP_NAME];
// Keep track of requests that have not been filled yet
static std::unordered_map<int, std::list<ServerResponse>> requests;
static std::multiset<InboxMessage> inbox;

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

    session_id = rng_dst(generator);

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
            //TODO
            break;
        case 'r':
            //TODO
            break;
        case 'v':
            //TODO
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
    static char mess[MAX_MESS_LEN];
    char sender[MAX_GROUP_NAME];
    char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    int num_groups;
    int service_type;
    int16_t mess_type;
    int endian_mismatch;
    int ret;

    ret = SP_receive(mbox, &service_type, sender, 100, &num_groups,
        target_groups, &mess_type, 
        &endian_mismatch, sizeof(mess), mess);
    
    if (ret < 0)
    {
        if ((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT))
        {
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive( mbox, &service_type, sender, MAX_MEMBERS, 
                &num_groups, target_groups, 
                &mess_type, &endian_mismatch, sizeof(mess), mess );
        }
    }
    
}

void log_in(const char * user)
{
    if (logged_in)
        log_out();
    
    username = std::string(user);
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
    session_id = rng_dst(generator);
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
    printf("Subject: ");
    if (fgets(msg.subject, MAX_SUBJECT, stdin) == NULL)
    {
        printf("Invalid subject\n");
        return;
    }
    printf("Message: ");
    if (fgets(msg.message, EMAIL_LEN, stdin) == NULL)
    {
        printf("Invalid message body\n");
        return;
    }
    msg.seq_num = seq_num++;
    strcpy(msg.username, username.c_str());

    requests[seq_num] = {};

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
    strcpy(msg.username, username.c_str());

    requests[seq_num] = {};

    SP_multicast(mbox, AGREED_MESS,
        connected_server_inbox.c_str(),
        MessageType::SHOW_INBOX,
        sizeof(msg),
        reinterpret_cast<const char*>(&msg)
    );
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