#include <stdint.h>
#include "../kernel/syscall.h"
#include "../user/lib/api.h"
#include "../user/lib/ustr.h"

#define strlen u_strlen
#define memset u_memset

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

static int command_is(const char *text, const char *name) {
    int index = 0;
    while (name[index]) {
        if (text[index] != name[index]) return 0;
        index++;
    }
    return text[index] == '\0' || text[index] == ' ';
}

static const char *error_text(uint32_t status) {
    switch (status) {
        case IPC_OK: return "OK";
        case IPC_ENOSVC: return "no such service";
        case IPC_EALREADY: return "service already running";
        case IPC_EBUSY: return "service busy";
        case IPC_EINVAL: return "invalid request";
        case IPC_ECONF: return "configuration error";
        case IPC_EVERSION: return "protocol version mismatch";
        default: return "init did not reply";
    }
}

static uint32_t send_request(uint8_t op, const char *service_name) {
    piux_ipc_msg_t request;
    memset(&request, 0, sizeof(request));
    request.magic = PIUX_IPC_MAGIC;
    request.version = PIUX_IPC_VERSION;
    request.op = op;
    request.seq = 1;
    request.arg_pid = (uint32_t)user_getpid();
    if (service_name) {
        int index = 0;
        while (service_name[index] && index < 31) {
            request.name[index] = service_name[index];
            index++;
        }
        request.name[index] = '\0';
    }
    if (user_ipc_send(1, &request) < 0) {
        puts_line("initctl: cannot reach PID 1");
        return IPC_EIO;
    }
    for (int attempt = 0; attempt < 100000; attempt++) {
        piux_ipc_msg_t response;
        if (user_ipc_try_recv(&response) == 0) {
            if (response.magic != PIUX_IPC_MAGIC || response.seq != request.seq ||
                response.op != (request.op | INITOP_REPLY)) {
                continue;
            }
            return (uint8_t)response.extra[0] |
                   ((uint32_t)(uint8_t)response.extra[1] << 8) |
                   ((uint32_t)(uint8_t)response.extra[2] << 16);
        }
    }
    return IPC_EIO;
}

static uint32_t send_status(const char *service_name, piux_ipc_msg_t *response) {
    piux_ipc_msg_t request;
    memset(&request, 0, sizeof(request));
    request.magic = PIUX_IPC_MAGIC;
    request.version = PIUX_IPC_VERSION;
    request.op = INITOP_STATUS;
    request.seq = 1;
    request.arg_pid = (uint32_t)user_getpid();
    if (service_name) {
        int index = 0;
        while (service_name[index] && index < 31) {
            request.name[index] = service_name[index];
            index++;
        }
        request.name[index] = '\0';
    }
    if (user_ipc_send(1, &request) < 0) {
        puts_line("initctl: cannot reach PID 1");
        return IPC_EIO;
    }
    for (int attempt = 0; attempt < 100000; attempt++) {
        if (user_ipc_try_recv(response) == 0) {
            if (response->magic == PIUX_IPC_MAGIC && response->seq == request.seq &&
                response->op == (request.op | INITOP_REPLY)) {
                return (uint8_t)response->extra[0];
            }
        }
    }
    return IPC_EIO;
}

static const char *state_text(int state) {
    switch (state) {
        case 0: return "stopped";
        case 1: return "starting";
        case 2: return "running";
        case 3: return "exited";
        default: return "unknown";
    }
}

static char program_args[128];

static int run(int argc, char **argv) {
    (void)argc;
    if (!argv[0]) {
        puts_line("Usage: initctl <start|stop|restart|reload|status> [service]");
        puts_line("       initctl reload (reload configuration)");
        return 2;
    }
    const char *command = argv[0];
    const char *service = argv[1];
    piux_ipc_msg_t response;

    if (command_is(command, "status")) {
        uint32_t status = send_status(service, &response);
        if (status != IPC_OK) {
            puts_text("initctl: ");
            puts_text(error_text(status));
            user_write("\n", 1);
            return 1;
        }
        if (service) {
            puts_text(service);
            puts_text(": ");
            puts_text(state_text((int)(uint8_t)response.extra[1]));
            puts_text(" pid=");
            puts_number((int)response.arg_pid);
            user_write("\n", 1);
        } else {
            puts_line("initctl: status without service is not supported yet");
        }
        return 0;
    }

    uint8_t op;
    if (command_is(command, "start")) op = INITOP_START;
    else if (command_is(command, "stop")) op = INITOP_STOP;
    else if (command_is(command, "restart")) op = INITOP_RESTART;
    else if (command_is(command, "reload")) op = service ? INITOP_RELOAD : INITOP_RELOAD_CONFIG;
    else {
        puts_line("Usage: initctl <start|stop|restart|reload|status> [service]");
        return 2;
    }

    if (op != INITOP_RELOAD_CONFIG && !service) {
        puts_line("initctl: missing service name");
        return 2;
    }
    if (op == INITOP_RELOAD_CONFIG && service) {
        puts_line("initctl: reload takes no service name");
        return 2;
    }

    uint32_t status = send_request(op, service);
    puts_text("initctl: ");
    puts_text(command);
    puts_text(" ");
    if (service) puts_text(service);
    puts_text(": ");
    puts_text(error_text(status));
    user_write("\n", 1);
    return status == IPC_OK ? 0 : 1;
}

static char program_args[128];

void _start(void) {
    char *argv[3];
    int argc = 0;
    char *cursor = program_args;
    user_getargs(program_args, sizeof(program_args));
    while (*cursor == ' ') cursor++;
    while (*cursor && argc < 3) {
        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ') cursor++;
        if (*cursor) *cursor++ = '\0';
        while (*cursor == ' ') cursor++;
    }
    user_exit(run(argc, argv));
    for (;;) { }
}
