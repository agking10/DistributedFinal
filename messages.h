#pragma once

#include "net_include.h"
#include <time.h>
#include <variant>

#define MAX_USERNAME 30
#define MAX_SUBJECT 100
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
    RESPONSE,

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
    char subject[MAX_SUBJECT];
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

struct InboxEntry
{
    bool read;
    time_t date_sent;
    char to[MAX_USERNAME];
    char from[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char message[MAX_MESS_LEN];
};

struct InboxMessage
{
    MessageType type = MessageType::INBOX;
    int seq_num;
    MessageIdentifier id;
    InboxEntry msg;
};

struct ServerResponse
{
    MessageType type = MessageType::RESPONSE;
    int seq_num;
    std::variant<
        AckMessage,
        InboxMessage
    > data;
};