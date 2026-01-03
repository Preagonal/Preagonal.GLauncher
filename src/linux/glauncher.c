#define _GNU_SOURCE
#include <dlfcn.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <libgen.h>
#include <limits.h>

struct server_entry {
    char ip[256];
    char port[16];
};

static struct server_entry servers[10];
static int server_count = 0;
static int selected_server = 0;
static int select_server_enabled = 0;

static struct hostent* (*real_gethostbyname)(const char*) = NULL;
static int (*real_getaddrinfo)(const char*, const char*, const struct addrinfo*, struct addrinfo**) = NULL;
static int (*real_connect)(int, const struct sockaddr*, socklen_t) = NULL;

static const char* graal_hosts[] = {
    "loginserver.graalonline.com",
    "Graalonline.com",
    "loginserver2.graalonline.com",
    "loginserver3.graalonline.com"
};

struct hostname_map {
    char ip[INET_ADDRSTRLEN];
    char original[256];
    char target[256];
};

static struct hostname_map hostname_cache[32];
static int cache_count = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void cache_hostname(const char* ip, const char* original, const char* target) {
    pthread_mutex_lock(&cache_mutex);
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(hostname_cache[i].ip, ip) == 0) {
            strncpy(hostname_cache[i].original, original, sizeof(hostname_cache[i].original) - 1);
            strncpy(hostname_cache[i].target, target, sizeof(hostname_cache[i].target) - 1);
            pthread_mutex_unlock(&cache_mutex);
            return;
        }
    }
    if (cache_count < 32) {
        strncpy(hostname_cache[cache_count].ip, ip, sizeof(hostname_cache[cache_count].ip) - 1);
        strncpy(hostname_cache[cache_count].original, original, sizeof(hostname_cache[cache_count].original) - 1);
        strncpy(hostname_cache[cache_count].target, target, sizeof(hostname_cache[cache_count].target) - 1);
        cache_count++;
    }
    pthread_mutex_unlock(&cache_mutex);
}

static int lookup_hostname(const char* ip, char* original, char* target) {
    pthread_mutex_lock(&cache_mutex);
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(hostname_cache[i].ip, ip) == 0) {
            strcpy(original, hostname_cache[i].original);
            strcpy(target, hostname_cache[i].target);
            pthread_mutex_unlock(&cache_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&cache_mutex);
    return 0;
}

struct hostent* gethostbyname(const char* name) {
    if (!real_gethostbyname)
        real_gethostbyname = dlsym(RTLD_NEXT, "gethostbyname");

    if (server_count > 0 && selected_server >= 0 && selected_server < server_count) {
        for (int i = 0; i < 4; i++) {
            if (strcmp(name, graal_hosts[i]) == 0) {
                struct hostent* result = real_gethostbyname(servers[selected_server].ip);
                if (result && result->h_addr_list[0]) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, result->h_addr_list[0], ip, INET_ADDRSTRLEN);
                    cache_hostname(ip, name, servers[selected_server].ip);
                }
                return result;
            }
        }
    }
    return real_gethostbyname(name);
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    if (!real_getaddrinfo)
        real_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");

    if (node && server_count > 0 && selected_server >= 0 && selected_server < server_count) {
        for (int i = 0; i < 4; i++) {
            if (strcmp(node, graal_hosts[i]) == 0)
                return real_getaddrinfo(servers[selected_server].ip, service, hints, res);
        }
    }
    return real_getaddrinfo(node, service, hints, res);
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    if (!real_connect)
        real_connect = dlsym(RTLD_NEXT, "connect");

    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        uint16_t port = ntohs(sin->sin_port);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip, INET_ADDRSTRLEN);

        if (server_count == 0 && port == 14900) {
            struct hostent* he = gethostbyname("listserver.graal.in");
            if (he && he->h_addr_list[0]) {
                char target_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, he->h_addr_list[0], target_ip, sizeof(target_ip));
                if (strcmp(ip, target_ip) == 0) {
                    sin->sin_port = htons(14911);
                    port = 14911;
                }
            }
        } else if (server_count > 0 && port == 14900) {
            int new_port = atoi(servers[selected_server].port);
            if (new_port > 0) {
                sin->sin_port = htons(new_port);
                port = new_port;
            }
        }

        char original_host[256], target_host[256];
        if (lookup_hostname(ip, original_host, target_host)) {
            printf("[GLAUNCHER] connect: %s:%d -> %s:%d\n", original_host, ntohs(sin->sin_port), target_host, port);
        }
    }
    return real_connect(sockfd, addr, addrlen);
}

static int read_license_file(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;

    server_count = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp) && server_count < 10) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0') continue;

        strncpy(servers[server_count].ip, line, sizeof(servers[server_count].ip) - 1);
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = 0;
            strncpy(servers[server_count].port, line, sizeof(servers[server_count].port) - 1);
            server_count++;
        }
    }
    fclose(fp);
    return server_count > 0 ? 0 : -1;
}

static void load_settings(void) {
    char config_path[PATH_MAX];
    const char* home = getenv("HOME");
    if (!home) return;

    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4", home);
    mkdir(config_path, 0755);
    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4/launcher.graal", home);

    FILE* fp = fopen(config_path, "r");
    if (fp) {
        char line[16];
        if (fgets(line, sizeof(line), fp))
            select_server_enabled = atoi(line);
        fclose(fp);
    }
}

static void save_settings(void) {
    char config_path[PATH_MAX];
    const char* home = getenv("HOME");
    if (!home) return;

    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4", home);
    mkdir(config_path, 0755);
    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4/launcher.graal", home);

    FILE* fp = fopen(config_path, "w");
    if (fp) {
        fprintf(fp, "%d\n", select_server_enabled);
        fclose(fp);
    }
}

static void on_server_selected(GtkWidget *widget, gpointer data) {
    int idx = GPOINTER_TO_INT(data);
    const char *text = gtk_button_get_label(GTK_BUTTON(widget));

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Connect to %s?", text
    );

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        selected_server = idx;
        gtk_main_quit();
    }
    gtk_widget_destroy(dialog);
}

static int show_server_dialog(const char* license_path) {
    if (license_path && read_license_file(license_path) != 0)
        return 0;

    if (server_count <= 1) {
        selected_server = 0;
        return 1;
    }

    gtk_init(NULL, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GLauncher - Server Selection");
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    GtkWidget *label = gtk_label_new("Select a server:");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    for (int i = 0; i < server_count; i++) {
        char text[512];
        snprintf(text, sizeof(text), "%s:%s", servers[i].ip, servers[i].port);
        GtkWidget *button = gtk_button_new_with_label(text);
        g_signal_connect(button, "clicked", G_CALLBACK(on_server_selected), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(window);
    gtk_main();
    gtk_widget_destroy(window);

    return selected_server >= 0;
}

static Display *display = NULL;
static Window root_window;
static int keyboard_active = 0;

static void* keyboard_monitor(void* arg) {
    char keymap[32];
    int ctrl = 0, shift = 0, l = 0;

    while (keyboard_active && display) {
        XQueryKeymap(display, keymap);

        int ctrl_key = XKeysymToKeycode(display, XK_Control_L);
        int shift_key = XKeysymToKeycode(display, XK_Shift_L);
        int l_key = XKeysymToKeycode(display, XK_L);

        int new_ctrl = keymap[ctrl_key / 8] & (1 << (ctrl_key % 8));
        int new_shift = keymap[shift_key / 8] & (1 << (shift_key % 8));
        int new_l = keymap[l_key / 8] & (1 << (l_key % 8));

        if (new_ctrl && new_shift && new_l && (!ctrl || !shift || !l)) {
            select_server_enabled = !select_server_enabled;
            save_settings();

            gtk_init(NULL, NULL);
            GtkWidget *dialog = gtk_message_dialog_new(
                NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                select_server_enabled ? 
                    "Server selection enabled" : 
                    "Server selection disabled"
            );
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }

        ctrl = new_ctrl;
        shift = new_shift;
        l = new_l;

        usleep(50000);
    }
    return NULL;
}

static pthread_t keyboard_thread;

static void start_keyboard_monitor(void) {
    display = XOpenDisplay(NULL);
    if (!display) return;

    root_window = DefaultRootWindow(display);
    keyboard_active = 1;
    pthread_create(&keyboard_thread, NULL, keyboard_monitor, NULL);
}

static void stop_keyboard_monitor(void) {
    keyboard_active = 0;
    if (keyboard_thread)
        pthread_join(keyboard_thread, NULL);
    if (display) {
        XCloseDisplay(display);
        display = NULL;
    }
}

static int is_graal_process(void) {
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return 0;
    path[len] = '\0';
    char* name = strrchr(path, '/');
    return name && (strcmp(name + 1, "Graal.exe") == 0 || strcmp(name + 1, "Graal") == 0);
}

__attribute__((constructor))
static void init(void) {
    if (!is_graal_process()) return;

    load_settings();
    start_keyboard_monitor();

    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char* last = strrchr(exe_path, '/');
        if (last) *last = '\0';
    }

    const char* paths[] = {
        "license.graal",
        "license/license.graal"
    };

    int loaded = 0;
    for (int i = 0; i < 2; i++) {
        char full_path[PATH_MAX];
        if (exe_path[0] && i < 2) {
            snprintf(full_path, sizeof(full_path), "%s/%s", exe_path, paths[i]);
            if (select_server_enabled) {
                if (show_server_dialog(full_path)) {
                    loaded = 1;
                    break;
                }
            }
            if (!loaded && read_license_file(full_path) == 0) {
                loaded = 1;
                break;
            }
        }
    }

    if (!loaded) {
        server_count = 1;
        selected_server = 0;
        strncpy(servers[0].ip, "listserver.graal.in", sizeof(servers[0].ip) - 1);
        strncpy(servers[0].port, "14911", sizeof(servers[0].port) - 1);
    }
}

__attribute__((destructor))
static void cleanup(void) {
    stop_keyboard_monitor();
}