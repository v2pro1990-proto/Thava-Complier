/* Platform operations for the embedded freestanding i686 runtime. */
#include "i686_os_ops.h"

#define STDCALL __attribute__((stdcall))

/* Preserve i386 C ABI registers across Linux's int 0x80 instruction. */
__attribute__((naked, noipa)) static int linux_call(u32 n, u32 a, u32 b, u32 c, u32 d, u32 e) {
    __asm__ volatile(
        "push %ebx\n\tpush %esi\n\tpush %edi\n\t"
        "mov 16(%esp),%eax\n\tmov 20(%esp),%ebx\n\tmov 24(%esp),%ecx\n\t"
        "mov 28(%esp),%edx\n\tmov 32(%esp),%esi\n\tmov 36(%esp),%edi\n\t"
        "int $0x80\n\tpop %edi\n\tpop %esi\n\tpop %ebx\n\tret");
}
#define LINUX(n,a,b,c) linux_call((n),(u32)(a),(u32)(b),(u32)(c),0,0)

static int valid_env_key(const char *key) {
    if (!*key) return 0;
    while (*key) if (*key++ == '=') return 0;
    return 1;
}

static int has_env_key(const char *entry, const char *key) {
    while (*key && *entry == *key) ++entry, ++key;
    return !*key && *entry == '=';
}

static char *env_assignment(const char *key, const char *value, Allocate allocate, u32 image) {
    u32 n = bytes(key), m = bytes(value);
    if (n >= ~0u - 1 || m > ~0u - n - 2) return 0;
    char *result = allocate(n + m + 2, image);
    if (!result) return 0;
    char *out = copy(result, key, n);
    *out++ = '=';
    copy(out, value, m + 1);
    return result;
}

static int linux_setenv(const char ***env, const char *key, const char *value,
                        Allocate allocate, u32 image) {
    if (!valid_env_key(key)) return 0;
    const char **old = *env;
    u32 n = 0, kept = 0;
    for (; old && old[n]; ++n) if (!has_env_key(old[n], key)) ++kept;
    if (kept == n && !*value) return 1;
    if (kept > (~0u / sizeof(*old)) - 2) return 0;
    const char **updated = (const char **)allocate((kept + !!*value + 1) * sizeof(*old), image);
    if (!updated) return 0;
    u32 next = 0;
    for (u32 i = 0; i < n; ++i) if (!has_env_key(old[i], key)) updated[next++] = old[i];
    if (*value) {
        char *entry = env_assignment(key, value, allocate, image);
        if (!entry) return 0;
        updated[next++] = entry;
    }
    updated[next] = 0;
    *env = updated;
    return 1;
}

static int dot_entry(const char *name) {
    return name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]));
}

static Collection *linux_list(const char *path, Allocate allocate, u32 image) {
    Collection *result = rt_collection_new(allocate, image);
    if (!result) return 0;
    int fd = LINUX(5, path, 0x10000, 0); /* open: O_RDONLY | O_DIRECTORY */
    if (fd < 0) return result;
    char buffer[2048];
    for (;;) {
        int count = LINUX(220, fd, buffer, sizeof(buffer)); /* getdents64 */
        if (count == -4) continue;
        if (count <= 0) break;
        u32 i = 0;
        while ((u32)count - i >= 20) {
            unsigned char *entry = (unsigned char *)buffer + i;
            u32 record = entry[16] | (entry[17] << 8);
            if (record < 20 || record > (u32)count - i) break;
            const char *name = (const char *)entry + 19;
            u32 len = 0;
            while (len < record - 19 && name[len]) ++len;
            if (len < record - 19 && !dot_entry(name))
                rt_list_insert(result, result->length, (u32)slice(name, len, allocate, image), allocate, image);
            i += record;
        }
    }
    LINUX(6, fd, 0, 0);
    return result;
}

typedef int (STDCALL *WinFindFirst)(const char *, void *);
typedef int (STDCALL *WinFindNext)(int, void *);
typedef int (STDCALL *WinFindClose)(int);

static Collection *windows_list(const char *path, WinFindFirst first, WinFindNext next,
                                WinFindClose close, Allocate allocate, u32 image) {
    Collection *result = rt_collection_new(allocate, image);
    if (!result) return 0;
    u32 n = bytes(path);
    if (!n) path = ".", n = 1;
    if (n > ~0u - 3) return result;
    char *pattern = allocate(n + 3, image);
    if (!pattern) return result;
    char *end = copy(pattern, path, n);
    if (end[-1] != '/' && end[-1] != '\\') *end++ = '\\';
    *end++ = '*';
    *end = 0;
    union { u32 alignment; char raw[320]; } data; /* WIN32_FIND_DATAA */
    int handle = first(pattern, data.raw);
    if (handle == -1) return result;
    do {
        const char *name = data.raw + 44;
        if (!dot_entry(name))
            rt_list_insert(result, result->length, (u32)slice(name, bytes(name), allocate, image), allocate, image);
    } while (next(handle, data.raw));
    close(handle);
    return result;
}

static u64 divide_u64_u32(u64 value, u32 divisor, u32 *remainder) {
    u32 hi = value >> 32, lo = value, quotient = hi / divisor, rest = hi % divisor;
    __asm__("divl %[den]" : "+a"(lo), "+d"(rest) : [den] "r"(divisor) : "cc");
    *remainder = rest;
    return ((u64)quotient << 32) | lo;
}

typedef struct { i64 sec, nsec; } Time64;
typedef struct { unsigned short year, month, weekday, day, hour, minute, second, millis; } Calendar;

static Time64 linux_clock(void) {
    Time64 now = {0, 0};
    if (LINUX(403, 0, &now, 0) < 0) {
        int old[2];
        if (LINUX(78, old, 0, 0) == 0) now.sec = old[0], now.nsec = (i64)old[1] * 1000;
    }
    return now;
}

static int leap(u32 year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

static Calendar utc_calendar(Time64 now) {
    Calendar cal = {0};
    u32 day_seconds, day_in_era;
    u64 total_days = divide_u64_u32(now.sec < 0 ? 0 : (u64)now.sec, 86400, &day_seconds);
    u64 eras = divide_u64_u32(total_days + 719162, 146097, &day_in_era);
    u32 year = (u32)(400 * eras) + 1, month = 1;
    while (day_in_era >= 365u + leap(year)) day_in_era -= 365u + leap(year), ++year;
    static const unsigned char days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    while (month <= 12 && day_in_era >= days[month - 1] + (u32)(month == 2 && leap(year))) {
        day_in_era -= days[month - 1] + (u32)(month == 2 && leap(year));
        ++month;
    }
    cal.year = year; cal.month = month; cal.day = day_in_era + 1;
    cal.hour = day_seconds / 3600; cal.minute = (day_seconds / 60) % 60;
    cal.second = day_seconds % 60; cal.millis = (u32)now.nsec / 1000000;
    return cal;
}

static char *write_digits(char *out, unsigned value, unsigned width) {
    for (unsigned i = width; i; --i) out[i - 1] = '0' + value % 10, value /= 10;
    return out + width;
}

static const char *format_calendar(Calendar cal, const char *pattern, Allocate allocate, u32 image) {
    if (equals_ascii_folded(pattern, "date")) pattern = "yyyy-mm-dd";
    else if (equals_ascii_folded(pattern, "time")) pattern = "hh:mm:ss";
    else if (equals_ascii_folded(pattern, "datetime")) pattern = "yyyy-mm-dd hh:mm:ss";
    u32 n = bytes(pattern);
    if (n > (~0u - 1) / 2) return "";
    char *buffer = allocate(n * 2 + 1, image);
    if (!buffer) return "";
    char *out = buffer;
    int clock = 0;
    while (*pattern) {
        if (matches(pattern, "yyyy") || matches(pattern, "YYYY"))
            out = write_digits(out, cal.year, 4), pattern += 4;
        else if (matches(pattern, "%Y")) out = write_digits(out, cal.year, 4), pattern += 2;
        else if (matches(pattern, "%y") || matches(pattern, "yy") || matches(pattern, "YY"))
            out = write_digits(out, cal.year, 2), pattern += 2;
        else if (matches(pattern, "%m") || matches(pattern, "MM"))
            out = write_digits(out, cal.month, 2), pattern += 2;
        else if (matches(pattern, "mm"))
            out = write_digits(out, clock ? cal.minute : cal.month, 2), pattern += 2;
        else if (matches(pattern, "%d") || matches(pattern, "dd") || matches(pattern, "DD"))
            out = write_digits(out, cal.day, 2), pattern += 2;
        else if (matches(pattern, "%H") || matches(pattern, "hh") || matches(pattern, "HH"))
            out = write_digits(out, cal.hour, 2), pattern += 2, clock = 1;
        else if (matches(pattern, "%M")) out = write_digits(out, cal.minute, 2), pattern += 2;
        else if (matches(pattern, "%S") || matches(pattern, "ss") || matches(pattern, "SS"))
            out = write_digits(out, cal.second, 2), pattern += 2;
        else if (matches(pattern, "%f")) out = write_digits(out, cal.millis, 3), pattern += 2;
        else if (matches(pattern, "fff")) out = write_digits(out, cal.millis, 3), pattern += 3;
        else *out++ = *pattern++;
    }
    *out = 0;
    return buffer;
}

static unsigned calendar_field(Calendar cal, u32 index) {
    switch (index) {
    case 0: return cal.year;
    case 1: return cal.month;
    case 2: return cal.day;
    case 3: return cal.hour;
    case 4: return cal.minute;
    case 5: return cal.second;
    default: return cal.millis;
    }
}

static void linux_sleep(i64 millis) {
    if (millis <= 0) return;
    u32 remainder;
    Time64 request;
    request.sec = divide_u64_u32((u64)millis, 1000, &remainder);
    request.nsec = remainder * 1000000;
    int status;
    do { status = linux_call(407, 1, 0, (u32)&request, (u32)&request, 0); } while (status == -4);
    if (status == -38 && request.sec <= 0x7fffffff) {
        int old[] = {(int)request.sec, (int)request.nsec};
        while (LINUX(162, old, old, 0) == -4) {}
    }
}

static i64 join_signed(u32 lo, u32 hi) { return (i64)(((u64)hi << 32) | lo); }

u64 rt_linux_os(u32 operation, u32 a, u32 b, u32 c, u32 d, u32 imports, u32 unused1,
                  u32 unused2, Allocate allocate, u32 image) {
    (void)c; (void)d; (void)unused1; (void)unused2;
    const char *first = (const char *)a, *second = (const char *)b;
    const char ***environment = (const char ***)imports;
    switch (operation) {
    case OS_PID: return LINUX(20, 0, 0, 0);
    case OS_CPU: {
        unsigned char mask[512];
        int n = LINUX(242, 0, sizeof(mask), mask);
        u32 cpus = 0;
        for (int i = 0; i < n; ++i)
            for (unsigned x = mask[i]; x; x >>= 1) cpus += x & 1;
        return cpus ? cpus : 1;
    }
    case OS_CWD: {
        char *path = allocate(4096, image);
        return path && LINUX(183, path, 4096, 0) > 0 ? (u32)path : (u32)"";
    }
    case OS_CHDIR: return LINUX(12, first, 0, 0) == 0;
    case OS_MKDIR: return LINUX(39, first, 0777, 0) == 0;
    case OS_RMDIR: return LINUX(40, first, 0, 0) == 0;
    case OS_REMOVE: return LINUX(10, first, 0, 0) == 0 || LINUX(40, first, 0, 0) == 0;
    case OS_RENAME: return LINUX(38, first, second, 0) == 0;
    case OS_EXISTS: case OS_IS_FILE: case OS_IS_DIR: case OS_SIZE: {
        struct {
            u32 mask, blocksize;
            u64 attributes;
            u32 links, uid, gid;
            unsigned short mode, pad;
            u64 inode, size;
            char rest[208];
        } stat;
        int found = linux_call(383, (u32)-100, a, 0, 0x201, (u32)&stat) == 0; /* statx */
        if (operation == OS_SIZE) return found ? stat.size : (u64)-1;
        if (operation == OS_EXISTS) return found;
        return found && (stat.mode & 0170000) == (operation == OS_IS_DIR ? 0040000 : 0100000);
    }
    case OS_LIST: return (u32)linux_list(first, allocate, image);
    case OS_ENV:
        if (*environment)
            for (const char **p = *environment; *p; ++p)
                if (has_env_key(*p, first)) return (u32)(*p + bytes(first) + 1);
        return (u32)"";
    case OS_SETENV: return linux_setenv(environment, first, second, allocate, image);
    case OS_SYSTEM: {
        int child = LINUX(2, 0, 0, 0);
        if (child < 0) return (u64)-1;
        if (!child) {
            const char *args[] = {"sh", "-c", first, 0};
            LINUX(11, "/bin/sh", args, *environment);
            LINUX(1, 127, 0, 0);
            return (u64)-1;
        }
        int status = 0, waited;
        do { waited = LINUX(7, child, &status, 0); } while (waited == -4);
        return waited < 0 ? (u64)-1 : (u64)status;
    }
    case OS_SLEEP: linux_sleep(join_signed(a, b)); return 0;
    case OS_MESSAGE: return 0;
    case TIME_STAMP: return linux_clock().sec;
    case TIME_FORMAT: return (u32)format_calendar(utc_calendar(linux_clock()), first, allocate, image);
    case TIME_FIELD: return calendar_field(utc_calendar(linux_clock()), a);
    default: return 0;
    }
}

u64 rt_windows_os(u32 operation, u32 a, u32 b, u32 c, u32 d, u32 f1, u32 f2, u32 f3,
                    Allocate allocate, u32 image) {
    const char *first = (const char *)a, *second = (const char *)b;
    switch (operation) {
    case OS_PID: return ((u32 (STDCALL *)(void))f1)();
    case OS_CPU: {
        u32 cpus = ((u32 (STDCALL *)(unsigned short))f1)(0xffff);
        return cpus ? cpus : 1;
    }
    case OS_CWD: {
        char *path = allocate(32768, image);
        if (!path) return (u32)"";
        u32 length = ((u32 (STDCALL *)(u32, char *))f1)(32768, path);
        return length && length < 32768 ? (u32)path : (u32)"";
    }
    case OS_CHDIR: case OS_MKDIR: case OS_RMDIR: case OS_REMOVE:
        return ((int (*)(const char *))f1)(first) == 0;
    case OS_RENAME: return ((int (*)(const char *, const char *))f1)(first, second) == 0;
    case OS_EXISTS: case OS_IS_FILE: case OS_IS_DIR: case OS_SIZE: {
        u32 stat[9]; /* WIN32_FILE_ATTRIBUTE_DATA */
        int found = ((int (STDCALL *)(const char *, int, void *))f1)(first, 0, stat);
        if (operation == OS_SIZE) return found ? ((u64)stat[7] << 32) | stat[8] : (u64)-1;
        if (operation == OS_EXISTS) return found != 0;
        return found && ((stat[0] & 16) != 0) == (operation == OS_IS_DIR);
    }
    case OS_LIST: return (u32)windows_list(first, (WinFindFirst)f1, (WinFindNext)f2,
                                           (WinFindClose)f3, allocate, image);
    case OS_ENV: {
        const char *value = ((const char *(*)(const char *))f1)(first);
        return (u32)(value ? slice(value, bytes(value), allocate, image) : "");
    }
    case OS_SETENV: {
        if (!valid_env_key(first)) return 0;
        char *entry = env_assignment(first, second, allocate, image);
        return entry && ((int (*)(const char *))f1)(entry) == 0;
    }
    case OS_SYSTEM: return (i64)((int (*)(const char *))f1)(first);
    case OS_SLEEP: {
        i64 millis = join_signed(a, b);
        void (STDCALL *sleep)(u32) = (void (STDCALL *)(u32))f1;
        if (millis <= 0) return 0;
        while (millis > 0xfffffffeu) sleep(0xfffffffeu), millis -= 0xfffffffeu;
        sleep((u32)millis);
        return 0;
    }
    case OS_MESSAGE: {
        unsigned flags = 0x40000; /* MB_TOPMOST */
        if (equals_ascii_folded(first, "warning")) flags |= 0x30;
        else if (equals_ascii_folded(first, "error")) flags |= 0x10;
        else if (equals_ascii_folded(first, "question")) flags |= 0x20;
        else if (equals_ascii_folded(first, "info") || equals_ascii_folded(first, "information")) flags |= 0x40;
        const char *buttons = (const char *)d;
        if (equals_ascii_folded(buttons, "okcancel")) flags |= 1;
        else if (equals_ascii_folded(buttons, "yesnocancel")) flags |= 3;
        else if (equals_ascii_folded(buttons, "yesno")) flags |= 4;
        else if (equals_ascii_folded(buttons, "retrycancel")) flags |= 5;
        return ((int (STDCALL *)(void *, const char *, const char *, u32))f1)(0, (const char *)c, second, flags);
    }
    case TIME_STAMP: {
        u32 raw[2], rem;
        ((void (STDCALL *)(void *))f1)(raw);
        u64 ticks = (((u64)raw[1] << 32) | raw[0]) - 116444736000000000ULL;
        return divide_u64_u32(ticks, 10000000, &rem);
    }
    case TIME_FORMAT: case TIME_FIELD: {
        Calendar now;
        ((void (STDCALL *)(Calendar *))f1)(&now);
        return operation == TIME_FIELD ? calendar_field(now, a) :
                                        (u32)format_calendar(now, first, allocate, image);
    }
    default: return 0;
    }
}
