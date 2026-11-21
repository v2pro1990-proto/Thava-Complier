/* Freestanding i686 runtime. Regenerate with tools/generate_i686_runtime.py. */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef char *(*Allocate)(u32 bytes, u32 image_base);
typedef int (*ReadByte)(u32 image_base);
typedef union { double number; u64 bits; } Float;
typedef const u64 CaseRow[4];

static u32 bytes(const char *s) {
    u32 len = 0;
    while (s[len]) ++len;
    return len;
}

static char *copy(char *out, const char *s, u32 n) {
    while (n--) *out++ = *s++;
    return out;
}

static char *slice(const char *s, u32 n, Allocate allocate, u32 image) {
    char *out = allocate(n + 1, image);
    if (!out) return "";
    *copy(out, s, n) = 0;
    return out;
}

static int matches(const char *s, const char *part) {
    while (*part && *s == *part) ++s, ++part;
    return *part == 0;
}

u32 rt_len(const char *s) { return bytes(s); }

char *rt_terminate_line(char *s, u32 len) {
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r')) --len;
    s[len] = 0;
    return s;
}

char *rt_read_line(ReadByte read_byte, Allocate allocate, u32 image) {
    u32 capacity = 1024, n = 0;
    char *result = allocate(capacity, image);
    if (!result) return "";
    for (;;) {
        int ch = read_byte(image);
        if (ch < 0 || ch == '\n') break;
        if (n == capacity - 1) {
            if (capacity > (~0u >> 1)) break;
            char *larger = allocate(capacity * 2, image);
            if (!larger) break;
            copy(larger, result, n);
            result = larger;
            capacity *= 2;
        }
        result[n++] = ch;
    }
    return rt_terminate_line(result, n);
}

int rt_find(const char *s, const char *part) {
    const char *first = s;
    do {
        if (matches(s, part)) return s - first;
    } while (*s++);
    return -1;
}

int rt_contains(const char *s, const char *part) { return rt_find(s, part) >= 0; }

u32 rt_count(const char *s, const char *part) {
    u32 n = bytes(part), count = 0;
    if (!n) return 0;
    while (*s) {
        if (matches(s, part)) s += n, ++count;
        else ++s;
    }
    return count;
}

int rt_startswith(const char *s, const char *part) { return matches(s, part); }

int rt_endswith(const char *s, const char *part) {
    u32 n = bytes(s), m = bytes(part);
    return n >= m && matches(s + n - m, part);
}

char *rt_concat(const char *a, const char *b, Allocate allocate, u32 image) {
    u32 n = bytes(a), m = bytes(b);
    if (m >= ~0u - n) return "";
    char *out = allocate(n + m + 1, image);
    if (!out) return "";
    copy(copy(out, a, n), b, m + 1);
    return out;
}

char *rt_repeat(const char *s, i64 count, Allocate allocate, u32 image) {
    u32 n = bytes(s);
    if (!n || count <= 0 || (u64)count > (~0u - 1) / n) return "";
    char *out = allocate(n * (u32)count + 1, image);
    if (!out) return "";
    char *cursor = out;
    for (u32 i = 0; i < (u32)count; ++i) cursor = copy(cursor, s, n);
    *cursor = 0;
    return out;
}

char *rt_replace(const char *s, const char *part, const char *replacement, Allocate allocate, u32 image) {
    u32 n = bytes(s), from = bytes(part), to = bytes(replacement);
    if (!from) return (char *)s;
    u32 count = rt_count(s, part);
    if (!count) return (char *)s;
    u32 unchanged = n - count * from;
    if (to > (~0u - 1 - unchanged) / count) return "";
    char *out = allocate(unchanged + count * to + 1, image);
    if (!out) return "";
    char *cursor = out;
    while (*s) {
        if (matches(s, part)) {
            cursor = copy(cursor, replacement, to);
            s += from;
        } else *cursor++ = *s++;
    }
    *cursor = 0;
    return out;
}

static int space(unsigned char ch) { return ch == ' ' || (ch >= 9 && ch <= 13); }

char *rt_strip(const char *s, Allocate allocate, u32 image) {
    while (space(*s)) ++s;
    u32 n = bytes(s);
    while (n && space(s[n - 1])) --n;
    return slice(s, n, allocate, image);
}

char *rt_case(const char *s, u32 mode, const CaseRow *rows, u32 count,
              Allocate allocate, u32 image) {
    u32 n = bytes(s);
    if (!n || n > (~0u - 1) / 4) return "";
    char *out = allocate(4 * n + 1, image);
    if (!out) return "";
    char *cursor = out;
    int new_word = 1;
    while (*s) {
        u32 first = (unsigned char)*s++;
        u32 key = first;
        if (first >= 0xc2 && first <= 0xf4) {
            u32 width = first < 0xe0 ? 2 : (first < 0xf0 ? 3 : 4);
            for (u32 i = 1; i < width; ++i) {
                u32 ch = (unsigned char)*s;
                if ((ch & 0xc0) != 0x80) break;
                key |= ch << (8 * i);
                ++s;
            }
        }
        u64 mapped = key;
        u32 lo = 0, hi = count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            const u64 *row = rows[mid];
            if (row[0] < key) lo = mid + 1;
            else if (row[0] > key) hi = mid;
            else {
                if (mode == 3) {
                    if (row[3]) {
                        mapped = row[new_word ? 3 : 1];
                        new_word = 0;
                    } else new_word = 1;
                } else mapped = row[mode];
                break;
            }
        }
        if (lo == hi) new_word = 0;
        do {
            *cursor++ = (char)mapped;
            mapped >>= 8;
        } while (mapped);
    }
    *cursor = 0;
    return out;
}

u64 rt_float_cast(u64 bits, u32 operation) {
    Float value = {.bits = bits};
    if (operation == 0) {
        value.number = (double)(i64)bits;
    } else if (operation == 1) {
        /* Match truncation toward zero and the x86 invalid conversion value. */
        if (!(value.number >= -0x1p63 && value.number < 0x1p63)) return 1ULL << 63;
        return (i64)value.number;
    } else {
        volatile float rounded = (float)value.number;
        value.number = rounded;
    }
    return value.bits;
}

u64 rt_float_binary(u64 a, u64 b, u32 flags, u32 op) {
    Float lhs = {.bits = a}, rhs = {.bits = b}, result;
    double x = flags & 1 ? (double)(i64)a : lhs.number;
    double y = flags & 2 ? (double)(i64)b : rhs.number;
    switch (op) {
    case 0: result.number = x + y; break;
    case 1: result.number = x - y; break;
    case 2: result.number = x * y; break;
    case 3: result.number = x / y; break;
    case 4: return x == y;
    case 5: return x != y;
    case 6: return x < y;
    case 7: return x <= y;
    case 8: return x > y;
    default: return x >= y;
    }
    return result.bits;
}

/* Divide by ten using two 32-bit quotient steps, with no libgcc calls. */
static u32 digit(u64 *value) {
    u32 hi = *value >> 32, lo = *value;
    u32 upper = hi / 10, rem = hi % 10;
    __asm__("divl %[ten]" : "+a"(lo), "+d"(rem) : [ten] "r"(10u) : "cc");
    *value = ((u64)upper << 32) | lo;
    return rem;
}

char *rt_format_int(i64 value, Allocate allocate, u32 image) {
    char buffer[24], *end = buffer + sizeof(buffer), *cursor = end;
    u64 number = value < 0 ? 0 - (u64)value : (u64)value;
    do { *--cursor = '0' + digit(&number); } while (number);
    if (value < 0) *--cursor = '-';
    return slice(cursor, end - cursor, allocate, image);
}

char *rt_format_float(u64 bits, Allocate allocate, u32 image) {
    Float raw = {.bits = bits & ~(1ULL << 63)};
    char buffer[32], *out = buffer;
    if (bits >> 63) *out++ = '-';
    if ((raw.bits >> 52) == 0x7ff) {
        const char *text = (raw.bits & ((1ULL << 52) - 1)) ? "nan" : "inf";
        out = copy(out, text, 3);
        return slice(buffer, out - buffer, allocate, image);
    }
    if (!raw.bits) {
        *out++ = '0';
        return slice(buffer, out - buffer, allocate, image);
    }

    long double value = raw.number;
    int exp = 0;
    while (value >= 10.0L) value /= 10.0L, ++exp;
    while (value < 1.0L) value *= 10.0L, --exp;
    /* x87's default round-to-nearest, ties-to-even, as for printf %g. */
    long double scaled = value * 100000.0L;
    int fraction;
    __asm__("fistpl %0" : "=m"(fraction) : "t"(scaled) : "st");
    if (fraction == 1000000) fraction = 100000, ++exp;
    char digits[6];
    for (int i = 5; i >= 0; --i) digits[i] = '0' + fraction % 10, fraction /= 10;
    int end = 6;
    while (end > 1 && digits[end - 1] == '0') --end;
    if (exp < -4 || exp >= 6) {
        *out++ = digits[0];
        if (end > 1) *out++ = '.', out = copy(out, digits + 1, end - 1);
        *out++ = 'e';
        *out++ = exp < 0 ? '-' : '+';
        if (exp < 0) exp = -exp;
        if (exp >= 100) *out++ = '0' + exp / 100;
        *out++ = '0' + (exp / 10) % 10;
        *out++ = '0' + exp % 10;
    } else if (exp < 0) {
        *out++ = '0';
        *out++ = '.';
        for (int i = -1; i > exp; --i) *out++ = '0';
        out = copy(out, digits, end);
    } else {
        for (int i = 0; i <= exp; ++i) *out++ = i < end ? digits[i] : '0';
        if (end > exp + 1) *out++ = '.', out = copy(out, digits + exp + 1, end - exp - 1);
    }
    return slice(buffer, out - buffer, allocate, image);
}
