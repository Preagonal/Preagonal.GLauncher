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
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

#define DYLD_INTERPOSE(_replacement,_replacee) \
   __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
   __attribute__((section("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

extern const uint8_t g_con_png_data[], g_conf_gs_data[];
extern const uint32_t g_con_png_size, g_conf_gs_size;

static uint16_t g_http_server_port = 0;
static int http_server_fd = -1, server_count = 0, selected_server = 0, select_server_enabled = 0, dialog_shown = 0;

static struct hostent* (*real_gethostbyname)(const char*) = NULL;
static int (*real_getaddrinfo)(const char*, const char*, const struct addrinfo*, struct addrinfo**) = NULL;
static int (*real_connect)(int, const struct sockaddr*, socklen_t) = NULL;

static const char* graal_hosts[] = {
    "loginserver.graalonline.com",
    "listserver.graalonline.com",
    "loginserver2.graalonline.com",
    "loginserver3.graalonline.com",
};

static const char* connector_hosts[] = {
    "con.quattroplay.com",
    "con2.quattroplay.com"
};

struct server_entry { char ip[256]; char port[16]; };
static struct server_entry servers[10];

struct hostname_map { char ip[INET_ADDRSTRLEN]; char original[256]; char target[256]; };
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

static void* http_server_thread(void* arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(server_fd); return NULL; }
    socklen_t len = sizeof(addr);
    getsockname(server_fd, (struct sockaddr*)&addr, &len);
    g_http_server_port = ntohs(addr.sin_port);
    http_server_fd = server_fd;
    listen(server_fd, 10);
    fprintf(stderr, "[GLAUNCHER] HTTP server on 127.0.0.1:%d\n", g_http_server_port);
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) break;
        char buffer[1024];
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            const uint8_t* data = NULL;
            uint32_t size = 0;
            const char* content_type = "application/octet-stream";
            if (strstr(buffer, "GET /conf.gs")) { data = g_conf_gs_data; size = g_conf_gs_size; }
            else if (strstr(buffer, "GET /con.png")) { data = g_con_png_data; size = g_con_png_size; }
            if (data && size > 0) {
                char header[512];
                snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", content_type, size);
                send(client_fd, header, strlen(header), 0);
                send(client_fd, data, size, 0);
            } else {
                const char* not_found = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
                send(client_fd, not_found, strlen(not_found), 0);
            }
        }
        close(client_fd);
    }
    close(server_fd);
    http_server_fd = -1;
    return NULL;
}

struct hostent* my_gethostbyname(const char* name) {
    if (!real_gethostbyname) real_gethostbyname = dlsym(RTLD_NEXT, "gethostbyname");
    fprintf(stderr, "[GLAUNCHER] gethostbyname: %s\n", name);
    if (server_count > 0 && selected_server >= 0 && selected_server < server_count) {
        for (int i = 0; i < 4; i++) {
            if (strcmp(name, graal_hosts[i]) == 0) {
                fprintf(stderr, "[GLAUNCHER] Redirecting %s -> %s\n", name, servers[selected_server].ip);
                struct hostent* result = real_gethostbyname(servers[selected_server].ip);
                if (result && result->h_addr_list[0]) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, result->h_addr_list[0], ip, INET_ADDRSTRLEN);
                    cache_hostname(ip, name, servers[selected_server].ip);
                }
                return result;
            }
        }
        for (int i = 0; i < 2; i++) {
            if (strcmp(name, connector_hosts[i]) == 0) {
                fprintf(stderr, "[GLAUNCHER] Redirecting %s -> 127.0.0.1\n", name);
                struct hostent* result = real_gethostbyname("127.0.0.1");
                if (result && result->h_addr_list[0]) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, result->h_addr_list[0], ip, INET_ADDRSTRLEN);
                    cache_hostname(ip, name, "127.0.0.1");
                }
                return result;
            }
        }
    }
    return real_gethostbyname(name);
}

int my_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    if (!real_getaddrinfo) real_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");
    if (node) fprintf(stderr, "[GLAUNCHER] getaddrinfo: %s:%s\n", node, service ? service : "null");
    if (node && server_count > 0 && selected_server >= 0 && selected_server < server_count) {
        for (int i = 0; i < 4; i++) {
            if (strcmp(node, graal_hosts[i]) == 0) {
                fprintf(stderr, "[GLAUNCHER] Redirecting %s -> %s\n", node, servers[selected_server].ip);
                return real_getaddrinfo(servers[selected_server].ip, service, hints, res);
            }
        }
        for (int i = 0; i < 2; i++) {
            if (strcmp(node, connector_hosts[i]) == 0) {
                fprintf(stderr, "[GLAUNCHER] Redirecting %s -> 127.0.0.1\n", node);
                return real_getaddrinfo("127.0.0.1", service, hints, res);
            }
        }
    }
    return real_getaddrinfo(node, service, hints, res);
}

int my_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    if (!real_connect) real_connect = dlsym(RTLD_NEXT, "connect");
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        uint16_t port = ntohs(sin->sin_port);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip, INET_ADDRSTRLEN);
        if (server_count == 0 && port == 14900) {
            struct hostent* he = real_gethostbyname("listserver.graal.in");
            if (he && he->h_addr_list[0]) {
                char target_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, he->h_addr_list[0], target_ip, sizeof(target_ip));
                if (strcmp(ip, target_ip) == 0) { sin->sin_port = htons(14911); port = 14911; }
            }
        } else if (server_count > 0 && port == 14900) {
            int new_port = atoi(servers[selected_server].port);
            if (new_port > 0) { sin->sin_port = htons(new_port); port = new_port; }
        }
        if (port == 80 || port == 443) {
            if (strcmp(ip, "127.0.0.1") == 0) {
                sin->sin_port = htons(g_http_server_port);
                fprintf(stderr, "[GLAUNCHER] Redirected localhost HTTP to port %d\n", g_http_server_port);
            }
        }
        char original_host[256], target_host[256];
        if (lookup_hostname(ip, original_host, target_host)) {
            fprintf(stderr, "[GLAUNCHER] connect: %s:%d -> %s:%d\n", original_host, ntohs(sin->sin_port), target_host, port);
        } else {
            fprintf(stderr, "[GLAUNCHER] connect: %s:%d\n", ip, port);
        }
    }
    return real_connect(sockfd, addr, addrlen);
}

DYLD_INTERPOSE(my_gethostbyname, gethostbyname);
DYLD_INTERPOSE(my_getaddrinfo, getaddrinfo);
DYLD_INTERPOSE(my_connect, connect);

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
    const char* home = getenv("HOME");
    if (!home) return;
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4", home);
    mkdir(config_path, 0755);
    snprintf(config_path, sizeof(config_path), "%s/.graal/graal4/glauncher.json", home);
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        fp = fopen(config_path, "w");
        if (fp) { fprintf(fp, "{\"server_selection\":false}\n"); fclose(fp); }
        return;
    }
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"server_selection\":true") || strstr(line, "\"server_selection\": true")) select_server_enabled = 1;
    }
    fclose(fp);
}

static void show_dialog_and_select(void) {
    if (dialog_shown || server_count <= 1) return;
    dialog_shown = 1;
    @autoreleasepool {
        NSWindow* mainWindow = [[NSApplication sharedApplication] orderedWindows].firstObject;
        if (mainWindow) {
            fprintf(stderr, "[GLAUNCHER] Miniaturizing main window\n");
            [mainWindow miniaturize:nil];
        }
        
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Select Server"];
        [alert setInformativeText:@"Choose a server to connect to:"];
        for (int i = 0; i < server_count; i++) {
            char text[512];
            snprintf(text, sizeof(text), "%s:%s", servers[i].ip, servers[i].port);
            [alert addButtonWithTitle:[NSString stringWithUTF8String:text]];
        }
        [alert addButtonWithTitle:@"Cancel"];
        NSInteger result = [alert runModal];
        if (result >= NSAlertFirstButtonReturn && result < NSAlertFirstButtonReturn + server_count) {
            selected_server = (int)(result - NSAlertFirstButtonReturn);
        }
        fprintf(stderr, "[GLAUNCHER] Dialog result: %ld, selected_server: %d\n", (long)result, selected_server);
        
        if (mainWindow) {
            fprintf(stderr, "[GLAUNCHER] Deminiaturizing main window\n");
            [mainWindow deminiaturize:nil];
        }
    }
}

static int is_graal_process(void) {
    char path[1024];
    uint32_t bufsize = sizeof(path);
    if (_NSGetExecutablePath(path, &bufsize) != 0) return 0;
    char* name = strrchr(path, '/');
    if (!name) return 0;
    name++;
    return (strcmp(name, "Graal") == 0 || strcmp(name, "Graal.bin") == 0);
}

__attribute__((constructor))
static void init(void) {
    if (!is_graal_process()) return;
    fprintf(stderr, "[GLAUNCHER] Loaded into Graal (PID %d, PPID %d)\n", getpid(), getppid());
    load_settings();
    const char* home = getenv("HOME");
    char base_path[PATH_MAX];
    snprintf(base_path, sizeof(base_path), "%s/.graal/graal4", home ? home : "/tmp");
    mkdir(base_path, 0755);
    char lock_path[PATH_MAX];
    snprintf(lock_path, sizeof(lock_path), "%s/.dialog_lock", base_path);
    FILE* lock_file = fopen(lock_path, "r");
    if (lock_file) { fclose(lock_file); remove(lock_path); }
    pthread_t http_thread;
    pthread_create(&http_thread, NULL, http_server_thread, NULL);
    usleep(100000);
    snprintf(base_path, sizeof(base_path), "%s/.graal/graal4/license.graal", home ? home : "/tmp");
    if (read_license_file(base_path) == 0) {
        if (select_server_enabled && server_count > 1) {
            lock_file = fopen(lock_path, "w");
            if (lock_file) fclose(lock_file);
            [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:NO block:^(NSTimer *t) {
                show_dialog_and_select();
                const char* home = getenv("HOME");
                char lock_path[PATH_MAX];
                snprintf(lock_path, sizeof(lock_path), "%s/.graal/graal4/.dialog_lock", home ? home : "/tmp");
                remove(lock_path);
            }];
        } else {
            selected_server = 0;
        }
    } else {
        server_count = 1;
        selected_server = 0;
        strncpy(servers[0].ip, "listserver.graal.in", sizeof(servers[0].ip) - 1);
        strncpy(servers[0].port, "14900", sizeof(servers[0].port) - 1);
    }
    fprintf(stderr, "[GLAUNCHER] Initialized with %d server(s), selected: %d\n", server_count, selected_server);
}

__attribute__((destructor))
static void cleanup(void) {
    if (http_server_fd >= 0) { close(http_server_fd); http_server_fd = -1; }
}
