#include <stdint.h>
#include "../user/lib/api.h"
#include "../user/lib/ustr.h"
#include "init.h"

#define strlen u_strlen
#define memset u_memset
#define strncpy u_strncpy
#define strchr u_strchr
#define strncmp u_strncmp
#define strcmp u_strcmp

void _start(void) {
    user_exit(init_main());
    for (;;) { }
}

#define PIUX_IPC_MAGIC 0x50495058U
#define PIUX_IPC_VERSION 1

#define INITOP_START 1
#define INITOP_STOP 2
#define INITOP_RESTART 3
#define INITOP_RELOAD 4
#define INITOP_RELOAD_CONFIG 5
#define INITOP_STATUS 6
#define INITOP_PING 7
#define INITOP_REPLY 0x80

#define IPC_OK 0
#define IPC_ENOSVC 1
#define IPC_EALREADY 2
#define IPC_EBUSY 3
#define IPC_EINVAL 4
#define IPC_ECONF 5
#define IPC_EIO 6
#define IPC_EVERSION 7

typedef syscall_video_op_t piux_unused_t;

typedef enum {
    SERVICE_STOPPED,
    SERVICE_STARTING,
    SERVICE_RUNNING,
    SERVICE_EXITED
} service_state_t;

#define SERVICE_MAX 16
#define STARTUP_MAX 4
#define NAME_SIZE 32
#define LINE_SIZE 256

typedef struct {
    char name[NAME_SIZE];
    char exec[NAME_SIZE];
    service_state_t state;
    int pid;
    int exit_status;
} service_t;

static service_t services[SERVICE_MAX];
static int service_count;
static char startup_entries[STARTUP_MAX][NAME_SIZE];
static char startup_paths[STARTUP_MAX][NAME_SIZE];
static int startup_pids[STARTUP_MAX];
static int startup_count;
static uint32_t sequence;

static void puts_text(const char *text) {
    user_write(text, (int)strlen(text));
}

static void puts_line(const char *text) {
    puts_text(text);
    user_write("\n", 1);
}

static void puts_number(int value) {
    char digits[12];
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (value < 0) user_write("-", 1);
    if (magnitude == 0) {
        user_write("0", 1);
        return;
    }
    while (magnitude > 0) {
        digits[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (length > 0) user_write(&digits[--length], 1);
}

static void trim(char *text) {
    int length = (int)strlen(text);
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static service_t *find_service(const char *name) {
    for (int index = 0; index < service_count; index++) {
        if (strcmp(services[index].name, name) == 0) return &services[index];
    }
    return 0;
}

static service_t *add_service(const char *name) {
    if (find_service(name)) return find_service(name);
    if (service_count >= SERVICE_MAX) return 0;
    service_t *service = &services[service_count++];
    memset(service, 0, sizeof(*service));
    strncpy(service->name, name, NAME_SIZE - 1);
    service->state = SERVICE_STOPPED;
    return service;
}

static int start_service(service_t *service);

static void run_startup_entries(void) {
    for (int index = 0; index < startup_count; index++) {
        const char *name = startup_entries[index];
        const char *path = startup_paths[index][0] ? startup_paths[index] : 0;
        if (!path) {
            if (strcmp(name, "pde") == 0 || strcmp(name, "desktop") == 0) {
                path = "/bin/desktop.elf";
            } else if (strcmp(name, "hello") == 0) {
                path = "/usr/bin/hello";
            } else {
                continue;
            }
            strncpy(startup_paths[index], path, NAME_SIZE - 1);
        }
        puts_text("[ OK ] Started ");
        puts_text(name);
        user_write("\n", 1);
        {
            service_t *service = add_service(name);
            if (service) {
                strncpy(service->exec, path, NAME_SIZE - 1);
                start_service(service);
                if (index < STARTUP_MAX) startup_pids[index] = service->pid;
            }
        }
    }
}

static void respawn_startup_process(int pid) {
    for (int index = 0; index < startup_count; index++) {
        if (startup_pids[index] != pid) continue;
        puts_text("[ OK ] Restarted ");
        puts_text(startup_entries[index]);
        user_write("\n", 1);
        startup_pids[index] = user_spawn(startup_paths[index], 0);
        return;
    }
}

static void parse_config(const char *path) {
    static char buffer[2048];
    int fd = user_open(path, 0);
    int total = 0;
    if (fd < 0) {
        puts_text("[ WARN ] cannot open ");
        puts_text(path);
        user_write("\n", 1);
        return;
    }
    for (;;) {
        int chunk = user_read(fd, buffer + total, (int)sizeof(buffer) - 1 - total);
        if (chunk <= 0) break;
        total += chunk;
    }
    user_close(fd);
    puts_text("[ OK ] Mounted configuration ");
    puts_text(path);
    puts_text(" size=");
    puts_number(total);
    user_write("\n", 1);
    if (total <= 0) {
        puts_text("[ WARN ] empty ");
        puts_text(path);
        user_write("\n", 1);
        return;
    }
    buffer[total] = '\0';

    char *line = buffer;
    char *end = buffer + total;
    while (line < end) {
        char *newline = line;
        while (newline < end && *newline != '\n') newline++;
        char saved = *newline;
        *newline = '\0';
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor == '#' || *cursor == '\0') {
            if (!saved) break;
            line = newline + 1;
            continue;
        }
        if (*cursor == '[') {
            if (!saved) break;
            line = newline + 1;
            continue;
        }
        char *equal = 0;
        for (char *scan = cursor; *scan; scan++) {
            if (*scan == '=') {
                equal = scan;
                break;
            }
        }
        if (!equal && startup_count < STARTUP_MAX) {
            strncpy(startup_entries[startup_count], cursor, NAME_SIZE - 1);
            trim(startup_entries[startup_count]);
            if (startup_entries[startup_count][0]) startup_count++;
        }
        if (equal) {
            *equal = '\0';
            char *key = cursor;
            char *value = equal + 1;
            trim(key);
            while (*value == ' ' || *value == '\t') value++;
            trim(value);
            char *bracket = strchr(key, ':');
            if (bracket && strncmp(key, "SERVICE", 7) == 0) {
                char name[NAME_SIZE];
                int length = (int)strlen(bracket + 1);
                if (length > 0 && length < NAME_SIZE) {
                    strncpy(name, bracket + 1, NAME_SIZE - 1);
                    trim(name);
                    service_t *service = add_service(name);
                    if (service && strcmp(key, "exec") == 0) {
                        strncpy(service->exec, value, NAME_SIZE - 1);
                    }
                }
            }
        }
        if (!saved) break;
        line = newline + 1;
    }
}

static int start_service(service_t *service) {
    if (service->state == SERVICE_RUNNING) return IPC_EALREADY;
    if (!service->exec[0]) return IPC_EINVAL;
    int pid = user_spawn(service->exec, 0);
    if (pid < 0) return IPC_EIO;
    service->pid = pid;
    service->state = SERVICE_RUNNING;
    return IPC_OK;
}

static int stop_service(service_t *service) {
    if (service->state != SERVICE_RUNNING) return IPC_OK;
    service->state = SERVICE_STOPPED;
    service->pid = 0;
    return IPC_OK;
}

static void reply(piux_ipc_msg_t *request, uint32_t status, int pid) {
    piux_ipc_msg_t response;
    memset(&response, 0, sizeof(response));
    response.magic = PIUX_IPC_MAGIC;
    response.version = PIUX_IPC_VERSION;
    response.op = request->op | INITOP_REPLY;
    response.seq = request->seq;
    response.arg_pid = (uint32_t)pid;
    response.extra[0] = (char)status;
    response.extra[1] = (char)(status >> 8);
    response.extra[2] = (char)(status >> 16);
    response.extra[3] = (char)(status >> 24);
    strncpy(response.name, request->name, sizeof(response.name) - 1);
    user_ipc_send((int)request->arg_pid, &response);
}

static void handle_request(piux_ipc_msg_t *request) {
    service_t *service = 0;
    uint32_t status = IPC_OK;
    int pid = 0;

    if (request->version != PIUX_IPC_VERSION) {
        reply(request, IPC_EVERSION, 0);
        return;
    }
    if (request->op == INITOP_PING) {
        reply(request, IPC_OK, 0);
        return;
    }
    if (request->op == INITOP_RELOAD_CONFIG) {
        service_count = 0;
        parse_config("/etc/piux/init.conf");
        reply(request, IPC_OK, 0);
        return;
    }
    if (request->name[0]) {
        service = find_service(request->name);
        if (!service) {
            reply(request, IPC_ENOSVC, 0);
            return;
        }
    } else if (request->op != INITOP_RELOAD_CONFIG && request->op != INITOP_PING) {
        reply(request, IPC_EINVAL, 0);
        return;
    }
    switch (request->op) {
        case INITOP_START:
            status = (uint32_t)start_service(service);
            pid = service->pid;
            break;
        case INITOP_STOP:
            status = (uint32_t)stop_service(service);
            break;
        case INITOP_RESTART:
            stop_service(service);
            status = (uint32_t)start_service(service);
            pid = service->pid;
            break;
        case INITOP_RELOAD:
            status = (uint32_t)start_service(service);
            break;
        case INITOP_STATUS:
            status = IPC_OK;
            pid = service->pid;
            request->extra[0] = (char)service->state;
            break;
        default:
            status = IPC_EINVAL;
            break;
    }
    if (request->op == INITOP_STATUS) {
        piux_ipc_msg_t response;
        memset(&response, 0, sizeof(response));
        response.magic = PIUX_IPC_MAGIC;
        response.version = PIUX_IPC_VERSION;
        response.op = request->op | INITOP_REPLY;
        response.seq = request->seq;
        response.arg_pid = request->arg_pid;
        response.extra[0] = (char)IPC_OK;
        response.extra[1] = (char)service->state;
        response.extra[2] = (char)service->exit_status;
        strncpy(response.name, request->name, sizeof(response.name) - 1);
        user_ipc_send((int)request->arg_pid, &response);
        return;
    }
    reply(request, status, pid);
}

int init_main(void) {
    static const char banner[] = "Piux init starting\n";
    user_write(banner, (int)sizeof(banner) - 1);
    parse_config("/etc/piux/init.conf");

    for (int index = 0; index < service_count; index++) {
        service_t *service = &services[index];
        if (!service->exec[0]) continue;
        puts_text("[ OK ] Started ");
        puts_text(service->name);
        user_write("\n", 1);
        start_service(service);
    }
    if (service_count == 0) {
        puts_line("[ OK ] Reached target Startup");
    }
    run_startup_entries();

    for (;;) {
        piux_ipc_msg_t message;
        int status = 0;
        int exited = user_wait_nonblocking(&status);
        if (exited > 0) {
            if (status == PIUX_EXIT_TO_RECOVERY_SHELL) user_exit(0);
            int respawned = 0;
            puts_text("[ WARN ] process pid=");
            puts_number(exited);
            puts_text(" exited status=");
            puts_number(status);
            user_write("\n", 1);
            for (int index = 0; index < service_count; index++) {
                service_t *service = &services[index];
                if (service->pid == exited) {
                    service->state = SERVICE_EXITED;
                    service->exit_status = status;
                    for (int s = 0; s < startup_count; s++) {
                        if (startup_pids[s] == exited) {
                            puts_text("[ OK ] Restarted ");
                            puts_text(service->name);
                            user_write("\n", 1);
                            start_service(service);
                            startup_pids[s] = service->pid;
                            respawned = 1;
                            break;
                        }
                    }
                    break;
                }
            }
            (void)respawned;
            continue;
        }
        if (user_ipc_recv(&message) == 0) {
            handle_request(&message);
        }
    }
    return 0;
}
