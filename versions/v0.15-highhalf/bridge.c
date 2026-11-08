#include "bridge.h"
#include "serial.h"
#include "kjson.h"
#include "tools.h"
#include "telemetry.h"
#include "time.h"
#include "console.h"
#include "kstring.h"

static char line[BRIDGE_LINE_MAX];
static unsigned int line_len;
static int line_overflow;          /* 这一行已经超长，正在丢弃到换行为止 */
static int connected;
static unsigned int events_pushed; /* 事件日志里已经推送到第几条 */
static struct intuition last;

/* 错误代码的序号（protocol error 事件的 a） */
enum { PE_BAD_JSON = 1, PE_LINE_TOO_LONG, PE_UNKNOWN_TYPE, PE_VERSION_MISMATCH, PE_BAD_FIELDS };

/* ---------------- 发送 ---------------- */

static char outbuf[BRIDGE_LINE_MAX + 64];

/* 发一行协议消息：保证从行首开始；超过行上限的截断并补 "..." */
static void send_line(struct tool_out *o) {
    unsigned int max = BRIDGE_LINE_MAX - 1;
    serial_begin_record();
    if (o->len > max) {
        /* 截在一个不破坏 JSON 结构的位置做不到（行里可能有任意内容），
           协议文档已说明 output 不保证完整；宿主机按"行超长"处理 */
        o->p[max - 3] = '.'; o->p[max - 2] = '.'; o->p[max - 1] = '.'; o->p[max] = '\0';
    }
    serial_write_string(o->p);
    serial_write_char('\n');
}

static void begin(struct tool_out *o, const char *type) {
    tout_init(o, outbuf, sizeof(outbuf));
    tout_str(o, "@{\"type\":\"");
    tout_str(o, type);
    tout_str(o, "\"");
}

static void field_str(struct tool_out *o, const char *key, const char *val) {
    tout_str(o, ",\""); tout_str(o, key); tout_str(o, "\":");
    kjson_put_str(o, val);
}

static void field_num(struct tool_out *o, const char *key, unsigned int val) {
    tout_str(o, ",\""); tout_str(o, key); tout_str(o, "\":");
    tout_dec(o, val);
}

static void end(struct tool_out *o) {
    tout_str(o, "}");
    send_line(o);
}

static void send_error(const char *error, const char *extra_key, const char *extra_str, unsigned int extra_num, int code) {
    struct tool_out o;
    begin(&o, "error");
    field_str(&o, "error", error);
    if (extra_key && extra_str) field_str(&o, extra_key, extra_str);
    else if (extra_key) field_num(&o, extra_key, extra_num);
    end(&o);
    tel_event(EV_PROTOCOL_ERROR, (unsigned int) code);
}

/* ---------------- 握手 ---------------- */

extern const char *kernel_version_string(void);

static void send_welcome_and_tools(void) {
    struct tool_out o;
    int i;
    begin(&o, "welcome");
    field_num(&o, "version", BRIDGE_VERSION);
    field_str(&o, "kernel", kernel_version_string());
    field_num(&o, "devmode", (unsigned int) tools_devmode());
    field_num(&o, "uptime_ms", time_ms());
    end(&o);

    begin(&o, "tools");
    field_num(&o, "count", (unsigned int) tools_count());
    end(&o);
    for (i = 0; i < tools_count(); i++) {
        const struct tool *t = tools_get(i);
        begin(&o, "tool");
        field_str(&o, "name", t->name);
        field_str(&o, "description", t->description);
        field_str(&o, "params", t->params);
        field_str(&o, "level", tool_level_name(t->level));
        end(&o);
    }
    begin(&o, "tools_end");
    end(&o);
}

static void handle_hello(const struct kjson *j) {
    unsigned int v;
    if (!kjson_num(j, "version", &v) || v != BRIDGE_VERSION) {
        connected = 0;
        send_error("version_mismatch", "want", 0, BRIDGE_VERSION, PE_VERSION_MISMATCH);
        return;
    }
    connected = 1;
    events_pushed = tel_event_total();     /* 握手之前的旧事件不补推 */
    send_welcome_and_tools();
}

/* ---------------- 工具调用 ---------------- */

static char result_out[TOOL_OUT_MAX];

static void handle_call(const struct kjson *j) {
    unsigned int id;
    const char *name = kjson_str(j, "name"), *args = kjson_str(j, "args");
    const struct tool *t;
    enum tool_outcome r;
    struct tool_out o;

    if (!kjson_num(j, "id", &id) || !name) {
        send_error("bad_fields", "got", "call needs id (number) and name (string)", 0, PE_BAD_FIELDS);
        return;
    }
    if (!args) args = "";

    /* 需要确认的调用：先告诉宿主机"要等人按键了"，再弹框 */
    t = tools_find(name);
    if (t && tools_needs_confirm(t)) {
        begin(&o, "confirm");
        field_num(&o, "id", id);
        field_str(&o, "name", t->name);
        field_str(&o, "level", tool_level_name(t->level));
        end(&o);
    }

    r = tools_invoke(name, args, "agent", result_out);

    begin(&o, "result");
    field_num(&o, "id", id);
    field_str(&o, "outcome", tool_outcome_name(r));
    field_str(&o, "output", result_out);
    end(&o);
}

/* ---------------- 直觉判断 ---------------- */

/* 只留可打印 ASCII，别的换成 '?'；最多 max 个字符 */
static void copy_printable(char *dst, const char *src, unsigned int max) {
    unsigned int i;
    for (i = 0; i < max && src[i]; i++) {
        unsigned char c = (unsigned char) src[i];
        dst[i] = (c >= 0x20 && c <= 0x7E) ? (char) c : '?';
    }
    dst[i] = '\0';
}

static void handle_verdict(const struct kjson *j) {
    const char *cls = kjson_str(j, "class"), *note = kjson_str(j, "note");
    unsigned int conf;
    struct tool_out o;

    if (!cls || !kjson_num(j, "confidence", &conf)) {
        send_error("bad_fields", "got", "verdict needs class (string) and confidence (number)", 0, PE_BAD_FIELDS);
        return;
    }
    if (conf > 100) conf = 100;
    copy_printable(last.cls, cls, sizeof(last.cls) - 1);
    copy_printable(last.note, note ? note : "", sizeof(last.note) - 1);
    last.confidence = conf;
    last.t_ms = time_ms();
    last.count++;
    tel_event(EV_INTUITION, conf);

    /* 判断只是信息：显示一行，不自动做任何事 */
    if (!(last.cls[0] == 'n' && memcmp(last.cls, "normal", 7) == 0)) {
        console_set_color(VGA_LIGHT_MAGENTA);
        console_write("\n[intuition] ");
        console_write(last.cls);
        console_write(" (");
        console_write_dec(conf);
        console_write("%)");
        if (last.note[0]) { console_write(": "); console_write(last.note); }
        console_set_color(VGA_LIGHT_GREY);
        console_putc('\n');
    }

    begin(&o, "ack");
    field_str(&o, "what", "verdict");
    end(&o);
}

const struct intuition *bridge_last_verdict(void) { return &last; }

/* ---------------- 事件推送 ---------------- */

static void push_new_events(void) {
    static struct tel_event_rec ev[TEL_EVENTS];
    unsigned int total = tel_event_total(), n, i;
    struct tool_out o;
    if (!connected) { events_pushed = total; return; }
    if (total < events_pushed) events_pushed = 0;          /* 日志被 clear_events 清过 */
    if (total == events_pushed) return;
    n = total - events_pushed;
    if (n > TEL_EVENTS) n = TEL_EVENTS;                     /* 太久没推，最旧的已被覆盖 */
    n = (unsigned int) tel_get_events(ev, (int) n);
    for (i = 0; i < n; i++) {
        begin(&o, "event");
        field_num(&o, "t_ms", ev[i].t_ms);
        field_str(&o, "name", tel_event_name(ev[i].type));
        field_num(&o, "count", ev[i].count);
        field_num(&o, "a", ev[i].a);
        end(&o);
    }
    events_pushed = total;
}

/* ---------------- 接收：拼行、分发 ---------------- */

static void dispatch(char *text) {
    struct kjson j;
    const char *type;
    char head[41];
    unsigned int i;

    if (kjson_parse(text, &j) != 0) {
        for (i = 0; i < 40 && text[i]; i++) head[i] = ((unsigned char) text[i] >= 0x20 && text[i] != '"' && text[i] != '\\') ? text[i] : '?';
        head[i] = '\0';
        send_error("bad_json", "line", head, 0, PE_BAD_JSON);
        return;
    }
    type = kjson_str(&j, "type");
    if (!type) { send_error("unknown_type", "got", "(none)", 0, PE_UNKNOWN_TYPE); return; }

    if (strlen(type) == 5 && memcmp(type, "hello", 5) == 0) { handle_hello(&j); return; }
    if (!connected) return;                                  /* 没握手或版本不对：其余一律忽略 */
    if (strlen(type) == 4 && memcmp(type, "call", 4) == 0)    { handle_call(&j); return; }
    if (strlen(type) == 7 && memcmp(type, "verdict", 7) == 0) { handle_verdict(&j); return; }
    send_error("unknown_type", "got", type, 0, PE_UNKNOWN_TYPE);
}

void bridge_init(void) {
    serial_enable_rx();
}

int bridge_connected(void) { return connected; }

void bridge_poll(void) {
    int c;
    while ((c = serial_read_char()) >= 0) {
        if (c == '\n') {
            if (line_overflow) {
                line_overflow = 0;
                send_error("line_too_long", 0, 0, 0, PE_LINE_TOO_LONG);
            } else if (line_len) {
                line[line_len] = '\0';
                dispatch(line);
            }
            line_len = 0;
            continue;
        }
        if (line_overflow) continue;
        if (c == '\r') continue;
        if (line_len >= BRIDGE_LINE_MAX - 1) { line_overflow = 1; continue; }   /* 丢弃到换行为止 */
        line[line_len++] = (char) c;
    }
    push_new_events();
}
