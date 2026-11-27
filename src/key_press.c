#include "key_press.h"

#ifdef __cplusplus
typedef size_t KeyWord;
#else
typedef __UINTPTR_TYPE__ KeyWord;
#endif

#if defined(THAVA_KEY_NATIVE) && defined(__x86_64__)
#define KEY_CALL __attribute__((ms_abi, force_align_arg_pointer))
#else
#define KEY_CALL
#endif

struct KeyRecord {
    unsigned short event, padding;
    int down;
    unsigned short repeat, key, scan, character;
    unsigned int modifiers;
};

typedef int (KEY_CALL *KeyRead)(void *, struct KeyRecord *, unsigned int, unsigned int *);

struct KeyRequest {
    const void *filter;
    char *output;
    void *console;
    KeyRead read;
    KeyWord kind;
};

static const char special_keys[][16] = {
    "KEY_ENTER", "KEY_ESCAPE", "KEY_TAB", "KEY_BACKSPACE", "KEY_SPACE",
    "KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT",
    "KEY_HOME", "KEY_END", "KEY_INSERT", "KEY_DELETE", "KEY_PAGEUP", "KEY_PAGEDOWN",
    "KEY_F1", "KEY_F2", "KEY_F3", "KEY_F4", "KEY_F5", "KEY_F6",
    "KEY_F7", "KEY_F8", "KEY_F9", "KEY_F10", "KEY_F11", "KEY_F12"
};

enum {
    KEY_ENTER, KEY_ESCAPE, KEY_TAB, KEY_BACKSPACE, KEY_SPACE,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END,
    KEY_INSERT, KEY_DELETE, KEY_PAGEUP, KEY_PAGEDOWN, KEY_F1
};

static void key_copy(char *out, const char *text) {
    while ((*out++ = *text++)) {}
}

static int key_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

static int utf8_length(unsigned int first) {
    if (first < 0x80) return 1;
    if (first >= 0xc2 && first <= 0xdf) return 2;
    if (first < 0xf0 && first >= 0xe0) return 3;
    if (first <= 0xf4 && first >= 0xf0) return 4;
    return 0;
}

static unsigned int decode_key(const unsigned char *text) {
    const int size = utf8_length(text[0]);
    if (!size) return 0;
    unsigned int code = size == 1 ? text[0] : text[0] & (0x7f >> size);
    for (int i = 1; i < size; ++i) {
        if ((text[i] & 0xc0) != 0x80) return 0;
        code = (code << 6) | (text[i] & 0x3f);
    }
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
        (size == 2 && code < 0x80) || (size == 3 && code < 0x800) || (size == 4 && code < 0x10000))
        return 0;
    return code;
}

static int key_name(unsigned int code, char *out) {
    switch (code) {
        case 8: case 127: key_copy(out, special_keys[KEY_BACKSPACE]); return 1;
        case 9: key_copy(out, special_keys[KEY_TAB]); return 1;
        case 10: case 13: key_copy(out, special_keys[KEY_ENTER]); return 1;
        case 27: key_copy(out, special_keys[KEY_ESCAPE]); return 1;
        case 32: key_copy(out, special_keys[KEY_SPACE]); return 1;
    }
    if (code < 33 || (code >= 128 && code < 160)) return 0;
    if (code >= 'a' && code <= 'z') code -= 'a' - 'A';
    key_copy(out, "KEY_");
    unsigned char *p = (unsigned char *)out + 4;
    if (code < 128) *p++ = (unsigned char)code;
    else if (code < 0x800) {
        *p++ = 0xc0 | (code >> 6); *p++ = 0x80 | (code & 63);
    } else if (code < 0x10000) {
        *p++ = 0xe0 | (code >> 12); *p++ = 0x80 | ((code >> 6) & 63); *p++ = 0x80 | (code & 63);
    } else {
        *p++ = 0xf0 | (code >> 18); *p++ = 0x80 | ((code >> 12) & 63);
        *p++ = 0x80 | ((code >> 6) & 63); *p++ = 0x80 | (code & 63);
    }
    *p = 0;
    return 1;
}

static int valid_key(const char *value) {
    if (!value || value[0]!='K' || value[1]!='E' || value[2]!='Y' || value[3]!='_') return 0;
    for (unsigned int i = 0; i < sizeof(special_keys) / sizeof(*special_keys); ++i)
        if (key_equal(value, special_keys[i])) return 1;
    const unsigned char *p = (const unsigned char *)value + 4;
    const int size = utf8_length(*p);
    if (!size || !*p) return 0;
    for (int i = 1; i < size; ++i) if (!p[i]) return 0;
    if (p[size]) return 0;
    char normalized[THAVA_KEY_BUFFER];
    return key_name(decode_key(p), normalized) && key_equal(value, normalized);
}

static KeyWord key_count(const struct KeyRequest *request) {
    if (request->kind == THAVA_KEY_ONE) return 1;
    if (!request->filter) return 0;
#if defined(__cplusplus)
    return ((const std::vector<std::string> *)request->filter)->size();
#elif defined(__i386__)
    const KeyWord *p = (const KeyWord *)request->filter;
    return p[1] == ~(KeyWord)0 ? 0 : p[0];
#else
    const KeyWord *p = (const KeyWord *)request->filter;
    if (p[0] == (KeyWord)-2) p = (const KeyWord *)p[1];
    return p[0] == (KeyWord)-1 ? 0 : p[0];
#endif
}

static const char *key_filter(const struct KeyRequest *request, KeyWord i) {
    if (request->kind == THAVA_KEY_ONE) return (const char *)request->filter;
#if defined(__cplusplus)
    return (*(const std::vector<std::string> *)request->filter)[i].c_str();
#elif defined(__i386__)
    const KeyWord *p = (const KeyWord *)request->filter;
    const KeyWord *value = (const KeyWord *)p[2] + i * 6 + 3;
    return value[2] == 4 ? (const char *)value[0] : 0;
#else
    const KeyWord *p = (const KeyWord *)request->filter;
    if (p[0] == (KeyWord)-2) p = (const KeyWord *)p[1];
    return p[2 + i * 2] == 4 ? (const char *)p[1 + i * 2] : 0;
#endif
}

static int key_matches(const struct KeyRequest *request, KeyWord count) {
    if (request->kind == THAVA_KEY_ANY) return 1;
    for (KeyWord i = 0; i < count; ++i)
        if (key_equal(key_filter(request, i), request->output)) return 1;
    return 0;
}

#ifdef THAVA_KEY_WINDOWS

static int windows_special(unsigned short virtual_key) {
    switch (virtual_key) {
        case 0x08: return KEY_BACKSPACE;
        case 0x09: return KEY_TAB;
        case 0x0d: return KEY_ENTER;
        case 0x1b: return KEY_ESCAPE;
        case 0x20: return KEY_SPACE;
        case 0x21: return KEY_PAGEUP;
        case 0x22: return KEY_PAGEDOWN;
        case 0x23: return KEY_END;
        case 0x24: return KEY_HOME;
        case 0x25: return KEY_LEFT;
        case 0x26: return KEY_UP;
        case 0x27: return KEY_RIGHT;
        case 0x28: return KEY_DOWN;
        case 0x2d: return KEY_INSERT;
        case 0x2e: return KEY_DELETE;
        default: return virtual_key >= 0x70 && virtual_key <= 0x7b ? KEY_F1 + virtual_key - 0x70 : -1;
    }
}

static int wait_key(const struct KeyRequest *request, KeyWord count) {
    struct KeyRecord record;
    unsigned int received, pending = 0;
    while (request->read(request->console, &record, 1, &received) && received) {
        if (record.event != 1 || !record.down) continue;
        const int special = windows_special(record.key);
        unsigned int code = record.character;
        if (special >= 0) key_copy(request->output, special_keys[special]);
        else {
            if (code == 3) return THAVA_KEY_INTERRUPTED;
            if (code >= 0xd800 && code <= 0xdbff) { pending = code; continue; }
            if (code >= 0xdc00 && code <= 0xdfff) {
                if (!pending) continue;
                code = 0x10000 + ((pending - 0xd800) << 10) + code - 0xdc00;
            }
            if (!key_name(code, request->output)) { pending = 0; continue; }
        }
        pending = 0;
        if (key_matches(request, count)) return THAVA_KEY_OK;
    }
    return THAVA_KEY_TERMINAL;
}

#else

#ifdef __cplusplus
#include <poll.h>
#include <termios.h>
#include <unistd.h>

typedef struct termios KeyTermios;
static int get_terminal(KeyTermios *mode) { return tcgetattr(0, mode); }
static int set_terminal(const KeyTermios *mode) { return tcsetattr(0, TCSANOW, mode); }
static void adjust_terminal(KeyTermios *mode) {
    mode->c_lflag &= ~(ICANON | ECHO | ISIG);
    mode->c_cc[VMIN] = 1;
    mode->c_cc[VTIME] = 0;
}
static int key_ready(void) {
    struct pollfd fd = {0, POLLIN, 0};
    return poll(&fd, 45) > 0;
}
static int key_byte(unsigned char *byte) { return read(0, byte, 1) == 1; }
#else

static long key_syscall(long number, long a, long b, long c) {
#if defined(__x86_64__)
    long result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
#elif defined(__i386__)
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
#else
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = 0;
    register long x4 __asm__("x4") = 0;
    register long x8 __asm__("x8") = number;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory");
    return x0;
#endif
}

#if defined(__x86_64__)
enum { key_read_number = 0, key_ioctl_number = 16, key_poll_number = 7 };
#elif defined(__i386__)
enum { key_read_number = 3, key_ioctl_number = 54, key_poll_number = 168 };
#else
enum { key_read_number = 63, key_ioctl_number = 29, key_poll_number = 73 };
#endif

typedef struct { unsigned int flags[4]; unsigned char line, cc[19]; } KeyTermios;
static int get_terminal(KeyTermios *mode) { return (int)key_syscall(key_ioctl_number, 0, 0x5401, (long)mode); }
static int set_terminal(const KeyTermios *mode) { return (int)key_syscall(key_ioctl_number, 0, 0x5402, (long)mode); }
static void adjust_terminal(KeyTermios *mode) {
    mode->flags[3] &= ~11u; /* ISIG, ICANON, ECHO */
    mode->cc[6] = 1; /* VMIN */
    mode->cc[5] = 0; /* VTIME */
}
static int key_ready(void) {
    struct { int fd; short events, received; } fd = {0, 1, 0};
#if defined(__aarch64__)
    const long timeout[2] = {0, 45000000};
    return key_syscall(key_poll_number, (long)&fd, 1, (long)timeout) > 0;
#else
    return key_syscall(key_poll_number, (long)&fd, 1, 45) > 0;
#endif
}
static int key_byte(unsigned char *byte) { return key_syscall(key_read_number, 0, (long)byte, 1) == 1; }
#endif

static int decode_escape(void) {
    unsigned char c;
    if (!key_ready() || !key_byte(&c)) return KEY_ESCAPE;
    if (c != '[' && c != 'O') return -1;
    const int ss3 = c == 'O';
    unsigned int number = 0;
    int parameter = 1;
    for (int i = 0; i < 16; ++i) {
        if (!key_ready() || !key_byte(&c)) return -1;
        if (c >= '0' && c <= '9' && parameter) number = number * 10 + c - '0';
        else if (c == ';') parameter = 0;
        else if (c >= 0x40 && c <= 0x7e) {
            switch (c) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case 'P': case 'Q': case 'R': case 'S': return KEY_F1 + c - 'P';
                case '[':
                    if (ss3 || !key_ready() || !key_byte(&c) || c < 'A' || c > 'E') return -1;
                    return KEY_F1 + c - 'A';
                case '~':
                    switch (number) {
                        case 1: case 7: return KEY_HOME;
                        case 2: return KEY_INSERT;
                        case 3: return KEY_DELETE;
                        case 4: case 8: return KEY_END;
                        case 5: return KEY_PAGEUP;
                        case 6: return KEY_PAGEDOWN;
                        case 11: case 12: case 13: case 14: case 15: return KEY_F1 + (int)number - 11;
                        case 17: case 18: case 19: case 20: case 21: return KEY_F1 + (int)number - 12;
                        case 23: case 24: return KEY_F1 + (int)number - 13;
                    }
            }
            return -1;
        }
    }
    return -1;
}

static int wait_key(const struct KeyRequest *request, KeyWord count) {
    KeyTermios saved;
    if (get_terminal(&saved) != 0) return THAVA_KEY_TERMINAL;
    KeyTermios mode = saved;
    adjust_terminal(&mode);
    if (set_terminal(&mode) != 0) return THAVA_KEY_TERMINAL;
    int status = THAVA_KEY_TERMINAL;
    unsigned char bytes[4] = {0, 0, 0, 0};
    while (key_byte(bytes)) {
        if (bytes[0] == 3) { status = THAVA_KEY_INTERRUPTED; break; }
        if (bytes[0] == 27) {
            const int key = decode_escape();
            if (key < 0) continue;
            key_copy(request->output, special_keys[key]);
        } else {
            const int size = utf8_length(bytes[0]);
            int read = 1;
            while (read < size && key_ready() && key_byte(bytes + read)) ++read;
            if (!size || read != size || !key_name(decode_key(bytes), request->output)) continue;
        }
        if (key_matches(request, count)) { status = THAVA_KEY_OK; break; }
    }
    if (set_terminal(&saved) != 0) return THAVA_KEY_TERMINAL;
    return status;
}
#endif

KEY_CALL int thava_key_press(const struct KeyRequest *request) {
    const KeyWord count = request->kind == THAVA_KEY_ANY ? 0 : key_count(request);
    if (request->kind != THAVA_KEY_ANY && !count) return THAVA_KEY_FILTER;
    for (KeyWord i = 0; i < count; ++i)
        if (!valid_key(key_filter(request, i))) return THAVA_KEY_FILTER;
    return wait_key(request, count);
}
