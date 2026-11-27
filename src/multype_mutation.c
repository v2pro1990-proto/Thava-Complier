#include "multype_mutation.h"

typedef __UINT64_TYPE__ Word;
typedef __INT64_TYPE__ Index;
typedef struct { Word bits, tag; } Entry;
typedef struct { Index count; Entry entries[]; } Block;
typedef struct { Index marker; Block *block; Word capacity; } Handle;
typedef union { Word bits; double number; } Real;

#if defined(__x86_64__)
#define CALL __attribute__((ms_abi, force_align_arg_pointer))
#else
#define CALL
#endif
typedef void *(CALL *Allocate)(Word);
typedef struct {
    Handle *list;
    Index operation, index;
    Entry value;
    Allocate allocate;
    Entry previous;
} Mutation;

static Block *items(Handle *list) {
    return list && list->marker == THAVA_SEQUENCE_HANDLE ? list->block : (Block *)list;
}

static Entry unwrap(Entry value) {
    if (value.tag == THAVA_TAG_SEQUENCE) {
        Block *block = items((Handle *)value.bits);
        if (block && block->count == THAVA_SCALAR_BOX) return block->entries[0];
    }
    return value;
}

static Word length(const char *text) {
    Word n = 0;
    if (text) while (text[n]) ++n;
    return n;
}

static int equal(Entry left, Entry right, unsigned depth) {
    left = unwrap(left);
    right = unwrap(right);
    if (left.tag != right.tag) return 0;
    if (left.tag == THAVA_TAG_REAL)
        return (Real){.bits = left.bits}.number == (Real){.bits = right.bits}.number;
    if (left.bits == right.bits) return 1;
    if (left.tag == THAVA_TAG_STRING || left.tag == THAVA_TAG_DECIMAL) {
        const char *a = (const char *)left.bits, *b = (const char *)right.bits;
        if (!a || !b) return 0;
        while (*a && *a == *b) ++a, ++b;
        return *a == *b;
    }
    if (left.tag == THAVA_TAG_SEQUENCE && depth) {
        const Block *a = items((Handle *)left.bits), *b = items((Handle *)right.bits);
        if (!a || !b || a->count != b->count) return 0;
        for (Index i = 0; i < a->count; ++i)
            if (!equal(a->entries[i], b->entries[i], depth - 1)) return 0;
        return 1;
    }
    return 0;
}

static int reaches(Entry value, const Handle *target, unsigned depth) {
    if (value.tag != THAVA_TAG_SEQUENCE) return 0;
    if ((const Handle *)value.bits == target || !depth) return 1;
    const Block *block = items((Handle *)value.bits);
    if (!block) return 0;
    const Index count = block->count == THAVA_SCALAR_BOX ? 1 : block->count;
    for (Index i = 0; i < count; ++i)
        if (reaches(block->entries[i], target, depth - 1)) return 1;
    return 0;
}

static int numeric(Entry value) {
    return value.tag == THAVA_TAG_INT || value.tag == THAVA_TAG_REAL;
}

static double number(Entry value) {
    return value.tag == THAVA_TAG_REAL ? (Real){.bits = value.bits}.number : (double)(Index)value.bits;
}

static int combine(Entry *result, Entry left, Entry right, const Mutation *request) {
    const Index op = request->operation;
    left = unwrap(left);
    right = unwrap(right);
    if ((op == THAVA_LIST_ADD && left.tag == THAVA_TAG_STRING && right.tag == THAVA_TAG_STRING) ||
        (op == THAVA_LIST_MUL &&
         ((left.tag == THAVA_TAG_STRING && right.tag == THAVA_TAG_INT) ||
          (left.tag == THAVA_TAG_INT && right.tag == THAVA_TAG_STRING)))) {
        const int concat = op == THAVA_LIST_ADD;
        const Entry text = left.tag == THAVA_TAG_STRING ? left : right;
        const char *first = (const char *)text.bits;
        const char *second = concat ? (const char *)right.bits : 0;
        const Word a = length(first), b = length(second);
        const Index repeat = concat ? 1 : (Index)(left.tag == THAVA_TAG_INT ? left.bits : right.bits);
        const Word copies = repeat > 0 && a != 0 ? (Word)repeat : 0;
        if (copies && a > (~(Word)0 - b - 1) / copies) return THAVA_LIST_ALLOC;
        char *out = request->allocate(a * copies + b + 1);
        if (!out) return THAVA_LIST_ALLOC;
        Word offset = 0;
        for (Word c = 0; c < copies; ++c)
            for (Word i = 0; i < a; ++i) out[offset++] = first[i];
        for (Word i = 0; i < b; ++i) out[offset++] = second[i];
        out[offset] = 0;
        *result = (Entry){(Word)out, THAVA_TAG_STRING};
        return THAVA_LIST_OK;
    }
    if (!numeric(left) || !numeric(right)) return THAVA_LIST_TYPE;
    if (left.tag == THAVA_TAG_REAL || right.tag == THAVA_TAG_REAL) {
        const double a = number(left), b = number(right);
        double value;
        switch (op) {
            case THAVA_LIST_ADD: value = a + b; break;
            case THAVA_LIST_SUB: value = a - b; break;
            case THAVA_LIST_MUL: value = a * b; break;
            case THAVA_LIST_DIV:
                if (b == 0) return THAVA_LIST_ZERO;
                value = a / b;
                break;
            default: return THAVA_LIST_TYPE;
        }
        *result = (Entry){(Real){.number = value}.bits, THAVA_TAG_REAL};
        return THAVA_LIST_OK;
    }

    const Word a = left.bits, b = right.bits;
    Word value;
    switch (op) {
        case THAVA_LIST_ADD: value = a + b; break;
        case THAVA_LIST_SUB: value = a - b; break;
        case THAVA_LIST_MUL: value = a * b; break;
        case THAVA_LIST_DIV: case THAVA_LIST_MOD:
            if (!b) return THAVA_LIST_ZERO;
            if (a == ((Word)1 << 63) && (Index)b == -1) value = op == THAVA_LIST_MOD ? 0 : a;
            else value = op == THAVA_LIST_MOD ? (Word)((Index)a % (Index)b) : (Word)((Index)a / (Index)b);
            break;
        case THAVA_LIST_POW: {
            Word base = a, exponent = (Index)b >= 0 ? b : 0;
            value = (Index)b >= 0 ? 1 : 0;
            while (exponent) {
                if (exponent & 1) value *= base;
                exponent >>= 1;
                base *= base;
            }
            break;
        }
        default: return THAVA_LIST_TYPE;
    }
    *result = (Entry){value, THAVA_TAG_INT};
    return THAVA_LIST_OK;
}

CALL int thava_multype_mutate(Mutation *request) {
    Handle *list = request->list;
    Block *block = items(list);
    if (!block || block->count < 0) return THAVA_LIST_NOT_SEQUENCE;
    const Index op = request->operation, count = block->count;
    Index i = request->index;
    if (i < 0) i += count;
    if (op == THAVA_LIST_READ) {
        if (i < 0 || i >= count) return THAVA_LIST_BOUNDS;
        request->previous = block->entries[i];
        return THAVA_LIST_OK;
    }
    Entry value = unwrap(request->value);

    switch (op) {
        case THAVA_LIST_CLEAR:
            block->count = 0;
            return THAVA_LIST_OK;
        case THAVA_LIST_REMOVE_VALUE:
            for (i = 0; i < count; ++i)
                if (equal(block->entries[i], value, 32)) break;
            if (i == count) return THAVA_LIST_OK;
            break;
        case THAVA_LIST_REMOVE_AT:
            if (i < 0 || i >= count) return THAVA_LIST_BOUNDS;
            break;
        case THAVA_LIST_APPEND: case THAVA_LIST_INSERT:
            if (list->marker != THAVA_SEQUENCE_HANDLE) return THAVA_LIST_LEGACY;
            if (reaches(value, list, 32)) return THAVA_LIST_CYCLE;
            if (op == THAVA_LIST_APPEND || i > count) i = count;
            if (i < 0) i = 0;
            if ((Word)count == list->capacity) {
                if (list->capacity > (~(Word)0 - sizeof(Block)) / (2 * sizeof(Entry))) return THAVA_LIST_ALLOC;
                const Word capacity = list->capacity ? list->capacity * 2 : 4;
                Block *next = request->allocate(sizeof(Block) + capacity * sizeof(Entry));
                if (!next) return THAVA_LIST_ALLOC;
                next->count = count;
                for (Index j = 0; j < count; ++j) next->entries[j] = block->entries[j];
                list->block = block = next;
                list->capacity = capacity;
            }
            for (Index j = count; j > i; --j) block->entries[j] = block->entries[j - 1];
            block->entries[i] = value;
            block->count = count + 1;
            return THAVA_LIST_OK;
        default:
            if (i < 0 || i >= count) return THAVA_LIST_BOUNDS;
            if (op != THAVA_LIST_SET) {
                const int status = combine(&value, request->previous, value, request);
                if (status) return status;
            }
            if (reaches(value, list, 32)) return THAVA_LIST_CYCLE;
            block->entries[i] = value;
            return THAVA_LIST_OK;
    }
    for (Index j = i + 1; j < count; ++j) block->entries[j - 1] = block->entries[j];
    block->count = count - 1;
    return THAVA_LIST_OK;
}
