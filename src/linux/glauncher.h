#ifndef GRAAL_COMBINED_H
#define GRAAL_COMBINED_H

#include <stdint.h>

extern uint16_t g_http_server_port;

struct server_entry {
    char ip[256];
    char port[16];
};

extern struct server_entry* g_servers;
extern int g_server_count;
extern int g_selected_server;

struct license_data {
    char host_resolves[4][256];
    char host_port[16];
    char target_exe[256];
};

extern struct license_data g_license_data;

extern int should_show_dialog(void);
extern void set_dialog_toggle(int enabled);

#endif
