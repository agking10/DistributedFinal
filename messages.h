#pragma once

#include "net_include.h"
#include <time.h>
#include <variant>

#define MAX_USERNAME 30
#define MAX_SUBJECT 100
#define EMAIL_LEN 1000
#define N_MACHINES 5
#define MAX_MEMBERS 100

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

struct ClientMessage
{
    MessageType type;
    uint32_t session_id;
};

struct MessageIdentifier
{
    int index;
    int origin;

    friend bool operator<(const MessageIdentifier& m1, const MessageIdentifier& m2);
};

bool operator<(const MessageIdentifier& m1, const MessageIdentifier& m2)
{
    if (m1.index == m2.index)
        return m1.origin < m2.origin;
    else 
        return m1.index < m2.index;
}

struct ConnectMessage
{
    MessageType type = MessageType::CONNECT;
    uint32_t session_id;
};

struct MailMessage
{
    MessageType type = MessageType::MAIL;
    uint32_t session_id;
    int seq_num;
    char username[MAX_USERNAME];
    char to[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char message[EMAIL_LEN];
};

struct ReadMessage
{   
    MessageType type = MessageType::READ;
    uint32_t session_id;
    int seq_num;
    char username[MAX_USERNAME];
    MessageIdentifier id;
};

struct DeleteMessage
{
    MessageType type = MessageType::DELETE;
    uint32_t session_id;
    int seq_num;
    char username[MAX_USERNAME];
    MessageIdentifier id;
};

struct GetInboxMessage
{
    MessageType type = MessageType::SHOW_INBOX;
    uint32_t session_id;
    int seq_num;
    char username[MAX_USERNAME];
};

struct UserCommand
{
    MessageIdentifier id;
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
    int sender;
    int summary[N_MACHINES][N_MACHINES];
};

struct AckMessage
{
    MessageType type = MessageType::ACK;
    int seq_num;
    char body[300];
};

struct InboxEntry
{
    bool read;
    time_t date_sent;
    char to[MAX_USERNAME];
    char from[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char message[EMAIL_LEN];
};

struct InboxMessage
{
    MessageType type = MessageType::INBOX;
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