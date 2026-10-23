#include <stdint.h>
#include "auth.h"
#include "ext2.h"
#include "keyboard.h"

#ifdef AUTH_CENTER_WITH_VIDEO
#include "video.h"
#endif

#define AUTH_PASSWD_PATH      "/.config/passwd"
#define AUTH_RECORD_SIZE      256
#define AUTH_PASSWORD_SIZE    64
#define AUTH_PASSWORD_BUFFER  (AUTH_PASSWORD_SIZE + 1)
#define AUTH_HASH_SIZE        64
#define AUTH_DB_SIZE          1024
#define AUTH_BOX_WIDTH        52
#define AUTH_SCREEN_COLUMNS   80
#define AUTH_KEY_ESC          27
#define AUTH_KEY_DEL          127

#define SHA256_BLOCK_SIZE     64
#define SHA256_DIGEST_SIZE    32

static char current_username[AUTH_USERNAME_SIZE];
static int current_sudoer;

static char auth_db[AUTH_DB_SIZE];
static char auth_new_db[AUTH_DB_SIZE];

static auth_puts_t ui_puts;
static auth_putc_t ui_putc;

static int string_length(const char *text) {
    int length = 0;
    while (text[length]) length++;
    return length;
}

static int strings_equal(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static void wipe_buffer(void *buffer, int size) {
    volatile char *p = (volatile char *)buffer;
    for (int i = 0; i < size; i++) p[i] = 0;
}

static int constant_time_equal(const char *left, const char *right, int length) {
    uint8_t difference = 0;
    for (int i = 0; i < length; i++) difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0;
}

static int append_bytes(char *dst, int size, int *offset, const char *src, int count) {
    if (count < 0 || *offset + count >= size) return -1;
    for (int i = 0; i < count; i++) dst[*offset + i] = src[i];
    *offset += count;
    dst[*offset] = '\0';
    return 0;
}

static int append_text(char *dst, int size, int *offset, const char *text) {
    return append_bytes(dst, size, offset, text, string_length(text));
}

static int is_enter(int key) {
    return key == '\n' || key == '\r';
}

static uint32_t rotate_right(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (32 - amount));
}

static uint32_t read_word(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_word(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void sha256_transform(uint32_t state[8], const uint8_t block[SHA256_BLOCK_SIZE]) {
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) words[i] = read_word(block + i * 4);
    for (int i = 16; i < 64; i++) {
        uint32_t small_sigma0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t small_sigma1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + small_sigma0 + words[i - 7] + small_sigma1;
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t big_sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t big_sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary1 = h + big_sigma1 + choose + constants[i] + words[i];
        uint32_t temporary2 = big_sigma0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    wipe_buffer(words, sizeof(words));
}

static void sha256(const char *input, char output[65]) {
    uint8_t block[SHA256_BLOCK_SIZE];
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint32_t length = 0;
    uint32_t offset = 0;
    uint64_t bit_length;
    static const char hex[] = "0123456789abcdef";

    while (input[length]) length++;
    while (length - offset >= SHA256_BLOCK_SIZE) {
        sha256_transform(state, (const uint8_t *)input + offset);
        offset += SHA256_BLOCK_SIZE;
    }
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) block[i] = 0;
    for (uint32_t i = offset; i < length; i++) block[i - offset] = (uint8_t)input[i];
    block[length - offset] = 0x80;
    if (length - offset >= 56) {
        sha256_transform(state, block);
        for (int i = 0; i < SHA256_BLOCK_SIZE; i++) block[i] = 0;
    }
    bit_length = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) block[63 - i] = (uint8_t)(bit_length >> (i * 8));
    sha256_transform(state, block);
    for (int i = 0; i < 8; i++) {
        uint8_t digest[4];
        write_word(digest, state[i]);
        for (int j = 0; j < 4; j++) {
            output[(i * 4 + j) * 2] = hex[digest[j] >> 4];
            output[(i * 4 + j) * 2 + 1] = hex[digest[j] & 0x0f];
        }
    }
    output[64] = '\0';
    wipe_buffer(block, sizeof(block));
    wipe_buffer(state, sizeof(state));
}

typedef struct {
    int user_start;
    int user_length;
    int hash_start;
    int hash_length;
    int sudoer;
} auth_record_t;

static int load_database(void) {
    int bytes = ext2_read_file_by_path(AUTH_PASSWD_PATH, auth_db, sizeof(auth_db) - 1);
    if (bytes < 0) return -1;
    if (bytes > (int)sizeof(auth_db) - 1) bytes = (int)sizeof(auth_db) - 1;
    auth_db[bytes] = '\0';
    return bytes;
}

static int next_line(const char *db, int length, int *position, int *start, int *end) {
    int line_end;
    if (*position >= length) return 0;
    *start = *position;
    line_end = *start;
    while (line_end < length && db[line_end] != '\n') line_end++;
    *position = line_end < length ? line_end + 1 : line_end;
    *end = line_end;
    if (*end > *start && db[*end - 1] == '\r') (*end)--;
    return 1;
}

static int parse_record(const char *db, int start, int end, auth_record_t *record) {
    int first, second;
    if (start >= end || db[start] == '#') return 0;
    first = start;
    while (first < end && db[first] != ':') first++;
    if (first >= end) return 0;
    second = first + 1;
    while (second < end && db[second] != ':') second++;
    if (second >= end) return 0;
    if (first == start) return 0;
    record->user_start = start;
    record->user_length = first - start;
    record->hash_start = first + 1;
    record->hash_length = second - first - 1;
    record->sudoer = (second + 1 < end && db[second + 1] == '1');
    return 1;
}

static int record_has_username(const char *db, const auth_record_t *record, const char *username) {
    if (string_length(username) != record->user_length) return 0;
    for (int i = 0; i < record->user_length; i++) {
        if (db[record->user_start + i] != username[i]) return 0;
    }
    return 1;
}

static int valid_account_name(const char *username) {
    int length;
    if (!username) return 0;
    length = string_length(username);
    if (length == 0 || length >= AUTH_USERNAME_SIZE) return 0;
    for (int i = 0; i < length; i++) {
        char c = username[i];
        if (c < 33 || c > 126 || c == ':') return 0;
    }
    return username[0] != '#';
}

static int store_record(const char *username, const char *record_text) {
    int length = load_database();
    int position = 0, start, end, out = 0;
    int inode;

    if (length < 0) length = 0;
    auth_new_db[0] = '\0';

    while (next_line(auth_db, length, &position, &start, &end)) {
        auth_record_t record;
        if (parse_record(auth_db, start, end, &record) && record_has_username(auth_db, &record, username)) continue;
        if (end == start) continue;
        if (append_bytes(auth_new_db, sizeof(auth_new_db), &out, auth_db + start, end - start) < 0) return -1;
        if (append_text(auth_new_db, sizeof(auth_new_db), &out, "\n") < 0) return -1;
    }
    if (append_text(auth_new_db, sizeof(auth_new_db), &out, record_text) < 0) return -1;

    inode = ext2_find_inode_by_path(AUTH_PASSWD_PATH);
    if (inode < 0) {
        if (ext2_create_file_by_path(AUTH_PASSWD_PATH) < 0) return -1;
        inode = ext2_find_inode_by_path(AUTH_PASSWD_PATH);
        if (inode < 0) return -1;
    }
    return ext2_write_file((uint32_t)inode, auth_new_db, (uint32_t)out) < 0 ? -1 : 0;
}

int auth_create_user(const char *username, const char *password, int sudoer) {
    char password_hash[AUTH_HASH_SIZE + 1];
    char record[AUTH_RECORD_SIZE];
    int offset = 0;
    int result;

    if (!valid_account_name(username) || !password) return -1;

    sha256(password, password_hash);
    record[0] = '\0';
    if (append_text(record, sizeof(record), &offset, username) < 0 ||
        append_text(record, sizeof(record), &offset, ":") < 0 ||
        append_text(record, sizeof(record), &offset, password_hash) < 0 ||
        append_text(record, sizeof(record), &offset, sudoer ? ":1\n" : ":0\n") < 0) {
        wipe_buffer(password_hash, sizeof(password_hash));
        return -1;
    }
    result = store_record(username, record);
    wipe_buffer(password_hash, sizeof(password_hash));
    return result;
}

static int authenticate(const char *username, const char *password, int *sudoer) {
    char password_hash[AUTH_HASH_SIZE + 1];
    int length = load_database();
    int position = 0, start, end;
    int found = -1;

    sha256(password, password_hash);
    if (length >= 0) {
        while (next_line(auth_db, length, &position, &start, &end)) {
            auth_record_t record;
            if (!parse_record(auth_db, start, end, &record)) continue;
            if (!record_has_username(auth_db, &record, username)) continue;
            if (record.hash_length == AUTH_HASH_SIZE &&
                constant_time_equal(auth_db + record.hash_start, password_hash, AUTH_HASH_SIZE)) {
                *sudoer = record.sudoer;
                found = 0;
                break;
            }
        }
    }
    wipe_buffer(password_hash, sizeof(password_hash));
    return found;
}

static int ui_margin(void) {
#ifdef AUTH_CENTER_WITH_VIDEO
    int columns = (int)video_columns;
    if (columns <= 0) columns = AUTH_SCREEN_COLUMNS;
#else
    int columns = AUTH_SCREEN_COLUMNS;
#endif
    return columns > AUTH_BOX_WIDTH ? (columns - AUTH_BOX_WIDTH) / 2 : 0;
}

static void ui_spaces(int count) {
    for (int i = 0; i < count; i++) ui_putc(' ');
}

static void ui_border(void) {
    ui_spaces(ui_margin());
    ui_putc('+');
    for (int i = 0; i < AUTH_BOX_WIDTH - 2; i++) ui_putc('-');
    ui_putc('+');
    ui_putc('\n');
}

static void ui_row(const char *text) {
    int inner = AUTH_BOX_WIDTH - 2;
    int length = string_length(text);
    int left, right;
    if (length > inner) length = inner;
    left = (inner - length) / 2;
    right = inner - length - left;
    ui_spaces(ui_margin());
    ui_putc('|');
    ui_spaces(left);
    for (int i = 0; i < length; i++) ui_putc(text[i]);
    ui_spaces(right);
    ui_putc('|');
    ui_putc('\n');
}

static void ui_header(void) {
    ui_putc('\n');
    ui_border();
    ui_row("");
    ui_row("P I U X");
    ui_row("Secure Login");
    ui_row("");
    ui_border();
    ui_putc('\n');
}

static void ui_wait_enter(void) {
    int key;
    do {
        key = keyboard_read_char();
    } while (!is_enter(key));
}

static int read_input(const char *label, char *buffer, int size, int secret) {
    int length = 0;
    buffer[0] = '\0';
    ui_puts(label);
    while (1) {
        int key = keyboard_read_char();
        if (is_enter(key)) {
            buffer[length] = '\0';
            ui_putc('\n');
            return length;
        }
        if (key == AUTH_KEY_ESC) {
            wipe_buffer(buffer, size);
            ui_putc('\n');
            return -1;
        }
        if ((key == '\b' || key == AUTH_KEY_DEL) && length > 0) {
            length--;
            buffer[length] = '\0';
            ui_putc('\b');
        } else if (key >= 32 && key < 127 && length < size - 1) {
            buffer[length++] = (char)key;
            ui_putc(secret ? '*' : (char)key);
        }
    }
}

int auth_login(auth_clear_t clear, auth_puts_t puts, auth_putc_t putc) {
    char username[AUTH_USERNAME_SIZE];
    char password[AUTH_PASSWORD_BUFFER];
    int sudoer = 0;
    int failures = 0;

    if (!clear || !puts || !putc) return -1;
    ui_puts = puts;
    ui_putc = putc;

    while (1) {
        int length;
        int accepted;

        clear();
        ui_header();
        if (ext2_find_inode_by_path(AUTH_PASSWD_PATH) < 0) {
            puts("  [!] No accounts found: the system is not installed.\n\n");
        }
        if (failures > 0) {
            puts("  Failed attempts: ");
            putc((char)('0' + (failures > 9 ? 9 : failures)));
            puts(failures > 9 ? "+\n\n" : "\n\n");
        }
        puts("  [Esc] Clear the current field\n\n");

        length = read_input("  Username: ", username, sizeof(username), 0);
        if (length <= 0) continue;

        length = read_input("  Password: ", password, sizeof(password), 1);
        if (length < 0) {
            wipe_buffer(password, sizeof(password));
            continue;
        }

        accepted = authenticate(username, password, &sudoer) == 0;
        wipe_buffer(password, sizeof(password));

        if (accepted) {
            int index = 0;
            while (username[index] && index < AUTH_USERNAME_SIZE - 1) {
                current_username[index] = username[index];
                index++;
            }
            current_username[index] = '\0';
            current_sudoer = sudoer;
            clear();
            return 0;
        }

        failures++;
        puts("\n  [!] Login failed: wrong username or password.\n");
        puts("      Press Enter to try again.");
        ui_wait_enter();
    }
}

const char *auth_current_username(void) { return current_username; }

uint32_t auth_current_uid(void) {
    if (current_username[0] == '\0' || strings_equal(current_username, "root")) return 0;
    return 1000;
}

int auth_current_user_is_sudoer(void) { return current_sudoer; }