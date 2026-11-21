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

static int digit_char(unsigned char ch) { return ch >= '0' && ch <= '9'; }

i64 rt_parse_int(const char *s) {
    while (space(*s)) ++s;
    int negative = *s == '-';
    if (negative || *s == '+') ++s;
    u64 value = 0;
    while (digit_char(*s)) {
        unsigned digit = *s++ - '0';
        if (value > 922337203685477580ULL ||
            (value == 922337203685477580ULL && digit > 7u + (unsigned)negative))
            return 0;
        value = value * 10 + digit;
    }
    return (i64)(negative ? 0 - value : value);
}

static int equals_ascii_folded(const char *s, const char *lower) {
    while (*lower) {
        unsigned char ch = *s++;
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (ch != *lower++) return 0;
    }
    return *s == 0;
}

u32 rt_parse_bool(const char *s) {
    return equals_ascii_folded(s, "true") || equals_ascii_folded(s, "yes") ||
           equals_ascii_folded(s, "1");
}

u64 rt_parse_float(const char *s) {
    while (space(*s)) ++s;
    int negative = *s == '-';
    if (negative || *s == '+') ++s;
    Float result;
    if (equals_ascii_folded(s, "inf") || equals_ascii_folded(s, "infinity"))
        result.bits = 0x7ff0000000000000ULL;
    else if (equals_ascii_folded(s, "nan")) result.bits = 0x7ff8000000000000ULL;
    else {
        long double mantissa = 0;
        int exponent = 0, significant = 0, decimal = 0;
        for (;;) {
            if (*s == '.' && !decimal) { decimal = 1; ++s; continue; }
            if (!digit_char(*s)) break;
            unsigned digit = *s++ - '0';
            if (significant < 19) {
                if (significant || digit) ++significant;
                mantissa = mantissa * 10 + digit;
                if (decimal && exponent > -10000) --exponent;
            } else if (!decimal && exponent < 10000) ++exponent;
        }
        if (*s == 'e' || *s == 'E') {
            ++s;
            int down = *s == '-';
            if (down || *s == '+') ++s;
            int explicit_exp = 0;
            while (digit_char(*s)) {
                if (explicit_exp < 10000) explicit_exp = explicit_exp * 10 + (*s - '0');
                ++s;
            }
            exponent += down ? -explicit_exp : explicit_exp;
        }
        if (mantissa == 0 || exponent < -400) result.bits = 0;
        else if (exponent > 400) result.bits = 0x7ff0000000000000ULL;
        else {
            while (exponent < 0) mantissa /= 10, ++exponent;
            while (exponent > 0) mantissa *= 10, --exponent;
            result.number = (double)mantissa;
        }
    }
    return result.bits | ((u64)negative << 63);
}

u64 rt_int_pow(u64 base, i64 exponent) {
    u64 result = 1;
    while (exponent > 0) {
        if (exponent & 1) result *= base;
        exponent >>= 1;
        base *= base;
    }
    return result;
}

static const char *next_utf8(const char *s) {
    unsigned char first = *s++;
    unsigned width = first < 0xc2 || first > 0xf4 ? 1 :
                     first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    for (unsigned i = 1; i < width && ((unsigned char)*s & 0xc0) == 0x80; ++i) ++s;
    return s;
}

char *rt_substring(const char *s, i64 start, i64 end, Allocate allocate, u32 image) {
    if (start < 1) start = 1;
    if (end < start) return "";
    i64 index = 1;
    while (*s && index < start) s = next_utf8(s), ++index;
    const char *first = s;
    while (*s && index <= end) s = next_utf8(s), ++index;
    return slice(first, s - first, allocate, image);
}

char *rt_str_index(const char *s, i64 index, Allocate allocate, u32 image) {
    if (index < 0 || (u64)index >= bytes(s)) return "";
    return rt_substring(s, index + 1, index + 1, allocate, image);
}

typedef struct { u64 key, value; } Entry;
typedef struct { u32 length, capacity; Entry *entries; } Collection;

Collection *rt_collection_new(Allocate allocate, u32 image) {
    Collection *list = (Collection *)allocate(sizeof(Collection), image);
    if (list) list->length = list->capacity = 0, list->entries = 0;
    return list;
}

static int reserve(Collection *list, Allocate allocate, u32 image) {
    if (!list) return 0;
    if (list->length < list->capacity) return 1;
    u32 cap = list->capacity ? 2 * list->capacity : 8;
    if (cap <= list->length || cap > ~0u / sizeof(Entry)) return 0;
    Entry *next = (Entry *)allocate(cap * sizeof(Entry), image);
    if (!next) return 0;
    for (u32 i = 0; i < list->length; ++i) next[i] = list->entries[i];
    list->entries = next;
    list->capacity = cap;
    return 1;
}

static i64 position(Collection *list, i64 index) {
    return index < 0 ? index + list->length : index;
}

u32 rt_collection_len(Collection *list) { return list ? list->length : 0; }
void rt_collection_clear(Collection *list) { if (list) list->length = 0; }

u64 rt_list_get(Collection *list, i64 index, u64 fallback) {
    if (!list) return fallback;
    index = position(list, index);
    return index >= 0 && index < list->length ? list->entries[(u32)index].value : fallback;
}

void rt_list_set(Collection *list, i64 index, u64 value) {
    if (!list) return;
    index = position(list, index);
    if (index >= 0 && index < list->length) list->entries[(u32)index].value = value;
}

void rt_list_insert(Collection *list, i64 index, u64 value, Allocate allocate, u32 image) {
    if (!reserve(list, allocate, image)) return;
    index = position(list, index);
    if (index < 0) index = 0;
    if (index > list->length) index = list->length;
    for (u32 i = list->length; i > (u32)index; --i) list->entries[i] = list->entries[i - 1];
    list->entries[(u32)index].key = 0;
    list->entries[(u32)index].value = value;
    ++list->length;
}

static int scalar_equal(u64 a, u64 b, u32 kind) {
    if (kind == 1) {
        const char *x = (const char *)(u32)a, *y = (const char *)(u32)b;
        while (*x && *x == *y) ++x, ++y;
        return *x == *y;
    }
    if (kind == 2) {
        Float x = {.bits = a}, y = {.bits = b};
        return x.number == y.number;
    }
    return a == b;
}

static void erase(Collection *list, u32 index) {
    for (u32 i = index + 1; i < list->length; ++i) list->entries[i - 1] = list->entries[i];
    --list->length;
}

void rt_list_remove_at(Collection *list, i64 index) {
    if (!list) return;
    index = position(list, index);
    if (index >= 0 && index < list->length) erase(list, (u32)index);
}

int rt_list_find(Collection *list, u64 value, u32 kind) {
    for (u32 i = 0; list && i < list->length; ++i)
        if (scalar_equal(list->entries[i].value, value, kind)) return i;
    return -1;
}

void rt_list_remove_value(Collection *list, u64 value, u32 kind) {
    int index = rt_list_find(list, value, kind);
    if (index >= 0) erase(list, (u32)index);
}

static int dict_find(Collection *dict, u64 key, u32 kind) {
    for (u32 i = 0; dict && i < dict->length; ++i)
        if (scalar_equal(dict->entries[i].key, key, kind)) return i;
    return -1;
}

u32 rt_dict_has(Collection *dict, u64 key, u32 kind) {
    return dict_find(dict, key, kind) >= 0;
}

u64 rt_dict_get(Collection *dict, u64 key, u32 kind, u64 fallback) {
    int index = dict_find(dict, key, kind);
    return index < 0 ? fallback : dict->entries[index].value;
}

void rt_dict_remove(Collection *dict, u64 key, u32 kind) {
    int index = dict_find(dict, key, kind);
    if (index >= 0) erase(dict, (u32)index);
}

void rt_dict_set(Collection *dict, u64 key, u64 value, u32 kind, Allocate allocate, u32 image) {
    int index = dict_find(dict, key, kind);
    if (index < 0) {
        if (!reserve(dict, allocate, image)) return;
        index = dict->length++;
        dict->entries[index].key = key;
    }
    dict->entries[index].value = value;
}

Collection *rt_dict_project(Collection *dict, u32 keys, Allocate allocate, u32 image) {
    Collection *result = rt_collection_new(allocate, image);
    for (u32 i = 0; dict && i < dict->length; ++i)
        rt_list_insert(result, i, keys ? dict->entries[i].key : dict->entries[i].value, allocate, image);
    return result;
}

Collection *rt_split(const char *s, const char *delimiter, Allocate allocate, u32 image) {
    Collection *list = rt_collection_new(allocate, image);
    u32 n = bytes(delimiter), i = 0;
    if (!n) {
        while (*s) {
            const char *end = next_utf8(s);
            rt_list_insert(list, i++, (u32)slice(s, end - s, allocate, image), allocate, image);
            s = end;
        }
        return list;
    }
    const char *part = s;
    for (;;) {
        if (!*s || matches(s, delimiter)) {
            rt_list_insert(list, i++, (u32)slice(part, s - part, allocate, image), allocate, image);
            if (!*s) break;
            s += n;
            part = s;
        } else ++s;
    }
    return list;
}

char *rt_join(const char *delimiter, Collection *list, Allocate allocate, u32 image) {
    u32 size = 0, n = bytes(delimiter), length = rt_collection_len(list);
    for (u32 i = 0; i < length; ++i) {
        u32 item = bytes((const char *)(u32)list->entries[i].value);
        if (item >= ~0u - size) return "";
        size += item;
        if (i) {
            if (n >= ~0u - size) return "";
            size += n;
        }
    }
    char *result = allocate(size + 1, image);
    if (!result) return "";
    char *out = result;
    for (u32 i = 0; i < length; ++i) {
        if (i) out = copy(out, delimiter, n);
        const char *item = (const char *)(u32)list->entries[i].value;
        out = copy(out, item, bytes(item));
    }
    *out = 0;
    return result;
}

const char *rt_collection_format(Collection *collection, u32 format, Allocate allocate, u32 image) {
    u32 length = rt_collection_len(collection), size = 3;
    if (length > (~0u / 2) / sizeof(const char *)) return "";
    const char **pieces = length ? (const char **)allocate(length * 2 * sizeof(*pieces), image) : 0;
    if (!pieces && length) return "";
    int dict = format & 0x100;
    unsigned kinds[] = {format & 15, (format >> 4) & 15};
    for (u32 i = 0; i < length; ++i) {
        u64 values[] = {collection->entries[i].key, collection->entries[i].value};
        for (int k = !dict; k < 2; ++k) {
            const char *piece;
            if (kinds[k] == 1) piece = (const char *)(u32)values[k];
            else if (kinds[k] == 2) piece = rt_format_float(values[k], allocate, image);
            else if (kinds[k] == 3) piece = values[k] ? "true" : "false";
            else piece = rt_format_int(values[k], allocate, image);
            pieces[i * 2 + k] = piece;
            u32 n = bytes(piece);
            if (n > ~0u - size - 4) return "";
            size += n + 4;
        }
    }
    char *result = allocate(size, image);
    if (!result) return "";
    char *out = result;
    *out++ = dict ? '{' : '[';
    for (u32 i = 0; i < length; ++i) {
        if (i) *out++ = ',', *out++ = ' ';
        for (int k = !dict; k < 2; ++k) {
            const char *piece = pieces[i * 2 + k];
            out = copy(out, piece, bytes(piece));
            if (k == 0) *out++ = ':', *out++ = ' ';
        }
    }
    *out++ = dict ? '}' : ']';
    *out = 0;
    return result;
}
