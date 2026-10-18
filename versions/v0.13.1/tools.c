#include "tools.h"
#include "confirm.h"
#include "console.h"
#include "time.h"
#include "telemetry.h"
#include "kstring.h"

#define MAX_TOOLS 32

static const struct tool *registry[MAX_TOOLS];
static int ntools;
static int devmode;

static struct audit_rec audit[AUDIT_RING];
static unsigned int audit_count;        /* 开机以来一共写了几条 */

/* ---------------- 登记表 ---------------- */

void tools_register(const struct tool *t) {
    if (ntools < MAX_TOOLS) registry[ntools++] = t;
}

int tools_count(void) { return ntools; }

const struct tool *tools_get(int index) {
    return (index >= 0 && index < ntools) ? registry[index] : 0;
}

const struct tool *tools_find(const char *name) {
    int i;
    unsigned int n = strlen(name);
    for (i = 0; i < ntools; i++) {
        if (strlen(registry[i]->name) == n && memcmp(registry[i]->name, name, n) == 0) return registry[i];
    }
    return 0;
}

const char *tool_level_name(enum tool_level level) {
    static const char *names[] = { "readonly", "reversible", "destructive", "system" };
    return (unsigned int) level < 4 ? names[level] : "?";
}

const char *tool_outcome_name(enum tool_outcome o) {
    static const char *names[] = { "ok", "failed", "denied_by_policy", "denied_by_user", "timeout", "unknown_tool", "bad_args" };
    return (unsigned int) o < 7 ? names[o] : "?";
}

/* ---------------- 审计 ---------------- */

static void audit_add(const char *tool, const char *caller, enum tool_level level,
                      enum tool_outcome outcome, const char *args) {
    struct audit_rec *r = &audit[audit_count % AUDIT_RING];
    unsigned int i;
    r->t_ms = time_ms();
    r->tool = tool;
    r->caller = caller;
    r->level = level;
    r->outcome = outcome;
    r->devmode = devmode;
    for (i = 0; i < AUDIT_ARGS - 1 && args[i]; i++) r->args[i] = args[i];
    r->args[i] = '\0';
    audit_count++;
}

int audit_get(struct audit_rec *out, int max) {
    unsigned int n = audit_count < AUDIT_RING ? audit_count : AUDIT_RING, i;
    unsigned int first = audit_count - n;
    if ((unsigned int) max < n) { first += n - (unsigned int) max; n = (unsigned int) max; }
    for (i = 0; i < n; i++) out[i] = audit[(first + i) % AUDIT_RING];
    return (int) n;
}

unsigned int audit_total(void) { return audit_count; }

/* ---------------- 权限判定 + 调用 ---------------- */

enum decision { DEC_ALLOW, DEC_CONFIRM, DEC_DENY };

/* 整个权限模型就是这一张表 */
static enum decision decide(enum tool_level level) {
    switch (level) {
    case TOOL_READONLY:    return DEC_ALLOW;
    case TOOL_REVERSIBLE:  return DEC_ALLOW;
    case TOOL_DESTRUCTIVE: return DEC_CONFIRM;
    case TOOL_SYSTEM:      return devmode ? DEC_CONFIRM : DEC_DENY;
    }
    return DEC_DENY;
}

static void copy_msg(char *out, const char *a, const char *b) {
    struct tool_out o;
    tout_init(&o, out, TOOL_OUT_MAX);
    tout_str(&o, a);
    if (b) tout_str(&o, b);
}

enum tool_outcome tools_invoke(const char *name, const char *args, const char *caller, char *out) {
    const struct tool *t = tools_find(name);
    enum tool_outcome outcome;
    char detail[128];   /* 够放最长的工具名 + 参数说明 + 等级 */
    int r;

    if (!args) args = "";
    if (!t) {
        copy_msg(out, "unknown tool: ", name);
        audit_add("?", caller, TOOL_READONLY, TOOL_UNKNOWN, name);
        return TOOL_UNKNOWN;
    }

    switch (decide(t->level)) {
    case DEC_DENY:
        copy_msg(out, "denied by policy: system-level tools need developer mode (devmode on)", 0);
        audit_add(t->name, caller, t->level, TOOL_DENIED_POLICY, args);
        tel_event(EV_TOOL_DENIED, (unsigned int) t->level);
        return TOOL_DENIED_POLICY;
    case DEC_CONFIRM: {
        struct tool_out o;
        tout_init(&o, detail, sizeof(detail));
        tout_str(&o, caller);
        tout_str(&o, " wants to run ");
        tout_str(&o, t->name);
        if (args[0]) { tout_str(&o, " "); tout_str(&o, args); }
        tout_str(&o, " [");
        tout_str(&o, tool_level_name(t->level));
        tout_str(&o, "]");
        r = confirm_ask(detail, t->description);
        if (r != 1) {
            outcome = r == 0 ? TOOL_DENIED_USER : TOOL_TIMEOUT;
            copy_msg(out, r == 0 ? "denied by user" : "confirmation timed out, denied", 0);
            audit_add(t->name, caller, t->level, outcome, args);
            tel_event(EV_TOOL_DENIED, (unsigned int) t->level);
            return outcome;
        }
        break;
    }
    case DEC_ALLOW:
        break;
    }

    out[0] = '\0';
    r = t->run(args, out, TOOL_OUT_MAX);
    outcome = r == 0 ? TOOL_OK : r == -2 ? TOOL_BAD_ARGS : TOOL_FAILED;
    if (r == -2 && !out[0]) copy_msg(out, "bad arguments; expected: ", t->params[0] ? t->params : "(none)");
    audit_add(t->name, caller, t->level, outcome, args);
    return outcome;
}

/* ---------------- 开发者模式 ---------------- */

int tools_devmode(void) { return devmode; }

int tools_set_devmode(int on) {
    if (on == devmode) return 1;
    if (on) {
        /* 开发者模式等于把系统级工具的大门打开，本身就要过确认框 */
        if (confirm_ask("shell wants to turn developer mode ON [system]",
                        "system-level tools (fault injection, ...) become available after confirmation") != 1) {
            audit_add("devmode", "shell", TOOL_SYSTEM, TOOL_DENIED_USER, "on");
            return 0;
        }
    }
    devmode = on;
    audit_add("devmode", "shell", TOOL_SYSTEM, TOOL_OK, on ? "on" : "off");
    tel_event(EV_DEVMODE, (unsigned int) on);
    return 1;
}

/* ---------------- 输出小工具 ---------------- */

void tout_init(struct tool_out *o, char *buf, unsigned int cap) {
    o->p = buf;
    o->cap = cap;
    o->len = 0;
    if (cap) buf[0] = '\0';
}

void tout_str(struct tool_out *o, const char *s) {
    while (*s && o->len + 1 < o->cap) o->p[o->len++] = *s++;
    if (o->cap) o->p[o->len] = '\0';
}

void tout_dec(struct tool_out *o, unsigned int n) {
    char d[10];
    int i = 0;
    do { d[i++] = (char) ('0' + n % 10); n /= 10; } while (n);
    while (i) { char c[2] = { d[--i], 0 }; tout_str(o, c); }
}

void tout_hex(struct tool_out *o, unsigned int n) {
    static const char hex[] = "0123456789ABCDEF";
    int s;
    tout_str(o, "0x");
    for (s = 28; s >= 0; s -= 4) { char c[2] = { hex[(n >> s) & 15], 0 }; tout_str(o, c); }
}
