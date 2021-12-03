#pragma once

#include "net_include.h"
#include "sp.h"
#include "messages.h"
#include <string>
#include <stdint.h>
#include <iostream>
#include <random>

void start();

static void handle_keyboard_in(int, int, void*);
static void handle_spread_message(int, int, void*);
void process_server_response(int16_t, const char *);
void process_membership_message(const char *, const char *, int, membership_info, int);
void log_in(const char *);
void log_out();
void connect_to_server(int);
void connection_success();
void disconnect();
void connect_failure_handler(int, void*);
void leave_current_session();
void send_email();
void get_inbox();
void goodbye();
void print_menu();
MessageIdentifier find_id_using_index(int index);
void read_email(int index);
void delete_email(int index);
void get_component();