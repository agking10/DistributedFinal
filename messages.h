#pragma once

#include "net_include.h"
#include <time.h>
#include <variant>

#define MAX_USERNAME 30
#define SUBJECT_LEN 100
#define EMAIL_LEN 1000
#define N_MACHINES 5

enum MessageType
{
    // Client to server messages
	CONNECT,
    MAIL,
	READ,
	DELETE,
	SHOW_INBOX,
    SHOW_COMPONENT,

    // Server to client message
	ACK,
	INBOX,

    // Server to server messages
	COMMAND,
	KNOWLEDGE
};

struct MessageHeader
{
    MessageType type;
};

struct MessageIdentifier
{
    int index;
    int origin;
};

struct ConnectMessage
{
    MessageType type = MessageType::CONNECT;
    uint32_t session_id;
};

struct MailMessage
{
    MessageType type = MessageType::MAIL;
    int seq_num;
    char username[MAX_USERNAME];
    char to[MAX_USERNAME];
    char subject[SUBJECT_LEN];
    char message[EMAIL_LEN];
};

struct ReadMessage
{   
    MessageType type = MessageType::READ;
    int seq_num;
    char username[MAX_USERNAME];
    MessageIdentifier id;
};

struct DeleteMessage
{
    MessageType type = MessageType::DELETE;
    int seq_num;
    char username[MAX_USERNAME];
    MessageIdentifier id;
};

struct GetInboxMessage
{
    MessageType type = MessageType::SHOW_INBOX;
    int seq_num;
    char username[MAX_USERNAME];
};

struct UserCommand
{
    MessageIdentifier id;
    int uid;
    time_t timestamp;
    std::variant<
        MailMessage, 
        ReadMessage, 
        DeleteMessage
    > data;
};

struct CommandMessage
{
    MessageType type = MessageType::COMMAND;
    UserCommand command;
};

struct KnowledgeMessage
{
    MessageType type = MessageType::KNOWLEDGE;
    int summary[N_MACHINES];
};

struct AckMessage
{
    MessageType type = MessageType::ACK;
    int seq_num;
};