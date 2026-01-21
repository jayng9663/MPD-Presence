#pragma once
#include <cstdint>
#include <string>

// RPC setup / teardown
void rpc_setup();
void rpc_initialize();
void rpc_shutdown();

// Update presence manually
void rpc_update_presence();

// Change details / state manually
void rpc_set_details(const char* details);
void rpc_set_state(const char* state);
void rpc_set_largeimagetext(const char* imagetext);
void rpc_set_starttime(int64_t starttime);
void rpc_set_endtime(int64_t endtime);
void rpc_set_largeimage(const std::string& url);

void rpc_load_button_settings();
void rpc_set_button1(const std::string& label, const std::string& url);
void rpc_set_button2(const std::string& label, const std::string& url);
