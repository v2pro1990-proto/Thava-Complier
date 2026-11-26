#include "decimal_spec.h"

enum { WORK_DIGITS = 2 * THAVA_DECIMAL_DIGITS + THAVA_DECIMAL_DIVISION_SCALE + 2 };

/* Least significant digit first. Magnitude and scale are independent. */
typedef struct {
    unsigned char digit[WORK_DIGITS];
    int length, scale, negative;
} Decimal;

static void normalize_magnitude(Decimal *value) {
    while (value->length > 0 && value->digit[value->length - 1] == 0) --value->length;
    if (value->length == 0) value->negative = 0;
}

static int parse_decimal(Decimal *value, const char *text) {
    value->length = value->scale = value->negative = 0;
    if (!text) return THAVA_DECIMAL_INVALID;
    if (*text == '-' || *text == '+') value->negative = *text++ == '-';
    int dot = 0;
    for (; *text; ++text) {
        if (*text == '.' && !dot) {
            dot = 1;
            continue;
        }
        if (*text < '0' || *text > '9') return THAVA_DECIMAL_INVALID;
        if (value->length == THAVA_DECIMAL_DIGITS) return THAVA_DECIMAL_OVERFLOW;
        value->digit[value->length++] = (unsigned char)(*text - '0');
        value->scale += dot;
    }
    if (!value->length) return THAVA_DECIMAL_INVALID;
    for (int left = 0, right = value->length - 1; left < right; ++left, --right) {
        unsigned char digit = value->digit[left];
        value->digit[left] = value->digit[right];
        value->digit[right] = digit;
    }
    normalize_magnitude(value);
    return 0;
}

static void shift(Decimal *value, int digits) {
    if (!value->length || !digits) return;
    for (int i = value->length; i-- > 0;) value->digit[i + digits] = value->digit[i];
    for (int i = 0; i < digits; ++i) value->digit[i] = 0;
    value->length += digits;
}

static int compare_magnitude(const Decimal *a, const Decimal *b) {
    if (a->length != b->length) return a->length < b->length ? -1 : 1;
    for (int i = a->length; i-- > 0;)
        if (a->digit[i] != b->digit[i]) return a->digit[i] < b->digit[i] ? -1 : 1;
    return 0;
}

static void subtract_magnitude(Decimal *a, const Decimal *b) {
    int borrow = 0;
    for (int i = 0; i < a->length; ++i) {
        int digit = a->digit[i] - (i < b->length ? b->digit[i] : 0) - borrow;
        borrow = digit < 0;
        a->digit[i] = (unsigned char)(digit + 10 * borrow);
    }
    normalize_magnitude(a);
}

static void add_magnitude(Decimal *a, const Decimal *b) {
    const int length = a->length > b->length ? a->length : b->length;
    int carry = 0;
    for (int i = 0; i < length; ++i) {
        int sum = (i < a->length ? a->digit[i] : 0) + (i < b->length ? b->digit[i] : 0) + carry;
        a->digit[i] = (unsigned char)(sum % 10);
        carry = sum / 10;
    }
    a->length = length;
    if (carry) a->digit[a->length++] = (unsigned char)carry;
}

static void multiply(Decimal *out, const Decimal *a, const Decimal *b) {
    out->length = a->length + b->length;
    out->scale = a->scale + b->scale;
    out->negative = a->negative != b->negative;
    for (int i = 0; i < out->length; ++i) out->digit[i] = 0;
    for (int i = 0; i < a->length; ++i) {
        int carry = 0;
        for (int j = 0; j < b->length; ++j) {
            const int sum = out->digit[i + j] + a->digit[i] * b->digit[j] + carry;
            out->digit[i + j] = (unsigned char)(sum % 10);
            carry = sum / 10;
        }
        out->digit[i + b->length] = (unsigned char)carry;
    }
    normalize_magnitude(out);
}

static void divide(Decimal *out, Decimal *a, Decimal *b) {
    Decimal remainder;
    remainder.length = remainder.scale = remainder.negative = 0;
    out->scale = THAVA_DECIMAL_DIVISION_SCALE;
    out->negative = a->negative != b->negative;
    const int exponent = out->scale + b->scale - a->scale;
    if (exponent >= 0) shift(a, exponent);
    else shift(b, -exponent);

    out->length = a->length;
    for (int i = a->length; i-- > 0;) {
        shift(&remainder, 1);
        remainder.digit[0] = a->digit[i];
        if (remainder.length == 0 && remainder.digit[0]) remainder.length = 1;
        unsigned char digit = 0;
        while (compare_magnitude(&remainder, b) >= 0) {
            subtract_magnitude(&remainder, b);
            ++digit;
        }
        out->digit[i] = digit;
    }
    normalize_magnitude(out);
}

static int format_decimal(const Decimal *value, char *text) {
    if (!value->length) {
        text[0] = '0';
        text[1] = 0;
        return 0;
    }
    int first = 0;
    while (first < value->scale && value->digit[first] == 0) ++first;
    const int high = value->length > value->scale ? value->length - 1 : value->scale;
    if (high - first + 1 > THAVA_DECIMAL_DIGITS) return THAVA_DECIMAL_OVERFLOW;

    if (value->negative) *text++ = '-';
    for (int i = high; i >= first; --i) {
        *text++ = (char)('0' + (i < value->length ? value->digit[i] : 0));
        if (i == value->scale && i > first) *text++ = '.';
    }
    *text = 0;
    return 0;
}

#if defined(THAVA_DECIMAL_NATIVE) && defined(__x86_64__)
__attribute__((ms_abi))
#endif
int thava_decimal_run(int op, const char *left, const char *right, char *text) {
    Decimal a, b, result;
    int status = parse_decimal(&a, left);
    if (status) return status;
    if (op == THAVA_DECIMAL_PARSE || op == THAVA_DECIMAL_NEGATE) {
        if (op == THAVA_DECIMAL_NEGATE && a.length) a.negative = !a.negative;
        return format_decimal(&a, text);
    }
    status = parse_decimal(&b, right);
    if (status) return status;
    if (op == THAVA_DECIMAL_MULTIPLY) {
        multiply(&result, &a, &b);
        return format_decimal(&result, text);
    }
    if (op == THAVA_DECIMAL_DIVIDE) {
        if (!b.length) return THAVA_DECIMAL_DIVIDE_BY_ZERO;
        divide(&result, &a, &b);
        return format_decimal(&result, text);
    }

    if (a.scale < b.scale) shift(&a, b.scale - a.scale), a.scale = b.scale;
    else if (b.scale < a.scale) shift(&b, a.scale - b.scale), b.scale = a.scale;
    int order = compare_magnitude(&a, &b);
    if (op >= THAVA_DECIMAL_EQUAL) {
        if (a.negative != b.negative) order = a.negative ? -1 : 1;
        else if (a.negative) order = -order;
        switch (op) {
            case THAVA_DECIMAL_EQUAL: return order == 0;
            case THAVA_DECIMAL_NOT_EQUAL: return order != 0;
            case THAVA_DECIMAL_LESS: return order < 0;
            case THAVA_DECIMAL_LESS_EQUAL: return order <= 0;
            case THAVA_DECIMAL_GREATER: return order > 0;
            case THAVA_DECIMAL_GREATER_EQUAL: return order >= 0;
            default: return THAVA_DECIMAL_INVALID;
        }
    }
    if (op == THAVA_DECIMAL_SUBTRACT && b.length) b.negative = !b.negative;
    if (a.negative == b.negative) {
        add_magnitude(&a, &b);
        return format_decimal(&a, text);
    }
    if (order >= 0) {
        subtract_magnitude(&a, &b);
        return format_decimal(&a, text);
    }
    subtract_magnitude(&b, &a);
    return format_decimal(&b, text);
}
