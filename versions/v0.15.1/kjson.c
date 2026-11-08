#include "kjson.h"
#include "tools.h"
#include "kstring.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    return p;
}

/* 解析从 *pp 开始的一个字符串字面量（*pp 指向开头的引号），就地解码，
   返回解码后字符串的起点；*pp 移到结尾引号之后。失败返回 0 */
static char *parse_string(char **pp) {
    char *src = *pp + 1, *dst = src, *start = src;
    while (*src && *src != '"') {
        if (*src == '\\') {
            src++;
            switch (*src) {
            case '"':  *dst++ = '"';  break;
            case '\\': *dst++ = '\\'; break;
            case 'n':  *dst++ = '\n'; break;
            case 't':  *dst++ = '\t'; break;
            case 'u': {
                int v = 0, i;
                for (i = 1; i <= 4; i++) {
                    int h = hexval(src[i]);
                    if (h < 0) return 0;
                    v = v * 16 + h;
                }
                src += 4;
                *dst++ = v < 0x80 ? (char) v : '?';   /* 子集只保证 ASCII；别的字符换成 ? */
                break;
            }
            default: return 0;
            }
            src++;
        } else {
            if ((unsigned char) *src < 0x20) return 0;   /* 字符串里不能有裸的控制字符 */
            *dst++ = *src++;
        }
    }
    if (*src != '"') return 0;
    *dst = '\0';
    *pp = src + 1;
    return start;
}

int kjson_parse(char *line, struct kjson *out) {
    char *p = skip_ws(line);
    out->n = 0;
    if (*p != '{') return -1;
    p = skip_ws(p + 1);
    if (*p == '}') return skip_ws(p + 1)[0] == '\0' ? 0 : -1;
    for (;;) {
        struct kjson_field *f;
        if (out->n >= KJSON_MAX_FIELDS) return -1;
        f = &out->f[out->n];
        if (*p != '"') return -1;
        f->key = parse_string(&p);
        if (!f->key) return -1;
        p = skip_ws(p);
        if (*p != ':') return -1;
        p = skip_ws(p + 1);
        if (*p == '"') {
            f->str = parse_string(&p);
            if (!f->str) return -1;
            f->num = 0;
            f->is_num = 0;
        } else if (*p >= '0' && *p <= '9') {
            unsigned int v = 0;
            while (*p >= '0' && *p <= '9') {
                if (v > 429496729U) return -1;           /* 会超出 32 位 */
                v = v * 10 + (unsigned int) (*p - '0');
                p++;
            }
            f->str = 0;
            f->num = v;
            f->is_num = 1;
        } else {
            return -1;                                  /* 嵌套、数组、true/false/null、负数、浮点：都不在子集里 */
        }
        out->n++;
        p = skip_ws(p);
        if (*p == ',') { p = skip_ws(p + 1); continue; }
        if (*p == '}') return skip_ws(p + 1)[0] == '\0' ? 0 : -1;
        return -1;
    }
}

const struct kjson_field *kjson_get(const struct kjson *j, const char *key) {
    int i;
    unsigned int n = strlen(key);
    for (i = 0; i < j->n; i++) {
        if (strlen(j->f[i].key) == n && memcmp(j->f[i].key, key, n) == 0) return &j->f[i];
    }
    return 0;
}

const char *kjson_str(const struct kjson *j, const char *key) {
    const struct kjson_field *f = kjson_get(j, key);
    return f && !f->is_num ? f->str : 0;
}

int kjson_num(const struct kjson *j, const char *key, unsigned int *v) {
    const struct kjson_field *f = kjson_get(j, key);
    if (!f || !f->is_num) return 0;
    *v = f->num;
    return 1;
}

void kjson_put_str(struct tool_out *o, const char *s) {
    static const char hex[] = "0123456789abcdef";
    char buf[7];
    tout_str(o, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c == '"')       tout_str(o, "\\\"");
        else if (c == '\\') tout_str(o, "\\\\");
        else if (c == '\n') tout_str(o, "\\n");
        else if (c == '\t') tout_str(o, "\\t");
        else if (c < 0x20 || c >= 0x7F) {
            buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0';
            buf[4] = hex[c >> 4]; buf[5] = hex[c & 15]; buf[6] = '\0';
            tout_str(o, buf);
        } else {
            buf[0] = (char) c; buf[1] = '\0';
            tout_str(o, buf);
        }
    }
    tout_str(o, "\"");
}
