#ifndef KJSON_H
#define KJSON_H

/* 协议用的最小 JSON 子集（v0.14，见 docs/agent-bridge-protocol.md 第 2 节）：
   一层对象，键是字符串，值是字符串或无符号整数；字符串转义只支持 \" \\ \n \t \uXXXX。
   没有嵌套、数组、浮点、布尔、null。

   解析是"就地"的：把输入行拆成若干 (键, 值) 对，字符串值解码后写回输入缓冲区里（解码后只会变短），
   所以 kjson_parse 之后输入缓冲区的内容已被改写，键和值的指针都指向它里面 */

#define KJSON_MAX_FIELDS 8

struct kjson_field {
    const char  *key;
    const char  *str;       /* 字符串值；整数值时为 0 */
    unsigned int num;       /* 整数值；字符串值时为 0 */
    int          is_num;
};

struct kjson {
    struct kjson_field f[KJSON_MAX_FIELDS];
    int n;
};

/* 返回 0 成功；-1 格式错误（不是这个子集里的 JSON） */
int                 kjson_parse(char *line, struct kjson *out);
const struct kjson_field *kjson_get(const struct kjson *j, const char *key);
const char         *kjson_str(const struct kjson *j, const char *key);   /* 没有或不是字符串返回 0 */
int                 kjson_num(const struct kjson *j, const char *key, unsigned int *v);   /* 1 = 有 */

/* 生成：往 tool_out 里追加一个 JSON 字符串字面量（带引号，做转义；非 ASCII 和控制字符转成 \u00XX） */
struct tool_out;
void kjson_put_str(struct tool_out *o, const char *s);

#endif
