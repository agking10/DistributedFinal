#include "server.h"
#include "messages.h"
#include <string>
#include "utils.hpp"


static mailbox mbox;
int server_index;
static char spread_name[80];
static sp_time test_timeout;
static char user[80];
static char private_group[MAX_GROUP_NAME];
int ret;
std::string name;


static char mess[MAX_MESS_LEN];
char sender[MAX_GROUP_NAME];
char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
int n_connected;
int service_type;
int16_t mess_type;
membership_info  memb_info;
int endian_mismatch;
int ret;

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Usage: <num messages> <process index> <num machines>"
        << std::endl;
    }
    server_index = std::stoi(argv[1]);

    start();
    recover();
    join();
    while (1) {
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

        if 
    }
}


void start()
{
    int ret;

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

}

void recover() {
    //TODO read log and json files
}

void join() {
    name = "all_servers";
    ret = SP_join(mbox, name.c_str());
    if (ret < 0) SP_error(ret);
    name = "server_" + std::to_string(server_index) + "_in";
    ret = SP_join(mbox, name.c_str());
    if (ret < 0) SP_error(ret);
}


int handle_command(char* mess, int mess_type) {
    int ret = 0;

    if (mess_type == MessageType::CONNECT) {
        ConnectMessage msg = 
        memcpy(reinterpret_cast<char *>(&msg), mess, sizeof(ConnectMessage));
        //reinterpret_cast<struct ConnectMessage>(mess);
        name = "client_" + std::to_string(msg.session_id) + "_connect";
        ret = SP_join(mbox, name.c_str());
            if (ret < 0) SP_error(ret);
    }

}