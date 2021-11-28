#pragma once

#include "net_include.h"
#include "sp.h"

#include <string>
#include <stdint.h>
#include <iostream>
#include <random>

void start();

static void handle_keyboard_in(int, int, void*);
static void handle_spread_message(int, int, void*);
void log_in(const char *);
void log_out();
void connect_to_server(int);
void disconnect();
void connect_failure_handler(int, void*);
void leave_current_session();
void send_email();
void get_inbox();
void goodbye();
