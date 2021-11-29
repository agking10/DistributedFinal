#pragma once

#include "net_include.h"
#include "sp.h"

#include <string>
#include <stdint.h>
#include <iostream>
#include <random>

#define MAX_MEMBERS 100

void start();

static void handle_keyboard_in(int, int, void*);
static void handle_spread_message(int, int, void*);
int knowledge [5][5];
void process_server_response(uint16_t, const char *);
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
