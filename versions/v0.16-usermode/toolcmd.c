#include "toolcmd.h"
#include "tools.h"
#include "console.h"
#include "kstring.h"

void cmd_tools(const char *args) {
    int i;
    (void) args;
    console_write("mode: ");
    console_write(tools_devmode() ? "DEVELOPER (system-level tools allowed after confirmation)\n"
                                  : "default (system-level tools denied; 'devmode on' to unlock)\n");
    for (i = 0; i < tools_count(); i++) {
        const struct tool *t = tools_get(i);
        unsigned int len = strlen(t->name);
        console_write("  ");
        console_write(t->name);
        while (len++ < 14) console_putc(' ');
        console_set_color(t->level == TOOL_READONLY ? VGA_LIGHT_GREEN : t->level == TOOL_REVERSIBLE ? VGA_LIGHT_CYAN
                        : t->level == TOOL_DESTRUCTIVE ? VGA_YELLOW : VGA_LIGHT_RED);
        console_write(tool_level_name(t->level));
        len = strlen(tool_level_name(t->level));
        console_set_color(VGA_LIGHT_GREY);
        while (len++ < 12) console_putc(' ');
        console_write(t->description);
        if (t->params[0]) { console_write("  args: "); console_write(t->params); }
        console_putc('\n');
    }
}

void cmd_tool(const char *args) {
    static char out[TOOL_OUT_MAX];
    char name[32];
    unsigned int n = 0;
    enum tool_outcome r;

    while (*args == ' ') args++;
    while (*args && *args != ' ' && n < sizeof(name) - 1) name[n++] = *args++;
    name[n] = '\0';
    while (*args == ' ') args++;
    if (!name[0]) { console_write("usage: tool <name> [args]   (see 'tools')\n"); return; }

    r = tools_invoke(name, args, "shell", out);
    console_set_color(r == TOOL_OK ? VGA_LIGHT_GREEN : VGA_LIGHT_RED);
    console_write(name);
    console_write(" -> ");
    console_write(tool_outcome_name(r));
    console_set_color(VGA_LIGHT_GREY);
    console_write(": ");
    console_write(out);
    console_putc('\n');
}

void cmd_devmode(const char *args) {
    if (args[0] == 'o' && args[1] == 'n' && args[2] == '\0') {
        console_write(tools_set_devmode(1) ? "developer mode ON\n" : "developer mode stays OFF (not confirmed)\n");
    } else if (args[0] == 'o' && args[1] == 'f' && args[2] == 'f' && args[3] == '\0') {
        tools_set_devmode(0);
        console_write("developer mode OFF\n");
    } else {
        console_write("developer mode is ");
        console_write(tools_devmode() ? "ON" : "OFF");
        console_write(" (usage: devmode on|off)\n");
    }
}

void cmd_audit(const char *args) {
    static struct audit_rec rec[AUDIT_RING];
    int n = audit_get(rec, AUDIT_RING), i;
    (void) args;
    console_write("audit: ");
    console_write_dec(audit_total());
    console_write(" calls since boot, showing last ");
    console_write_dec((unsigned int) n);
    console_write(":\n");
    for (i = 0; i < n; i++) {
        unsigned int len;
        console_write("  ");
        console_write_dec(rec[i].t_ms / 1000);
        console_putc('.');
        console_putc((char) ('0' + rec[i].t_ms % 1000 / 100));
        console_write("s ");
        console_write(rec[i].caller);
        console_write(rec[i].devmode ? " dev " : " def ");
        console_write(rec[i].tool);
        len = strlen(rec[i].tool);
        while (len++ < 14) console_putc(' ');
        console_write(tool_level_name(rec[i].level));
        len = strlen(tool_level_name(rec[i].level));
        while (len++ < 12) console_putc(' ');
        console_set_color(rec[i].outcome == TOOL_OK ? VGA_LIGHT_GREEN : VGA_LIGHT_RED);
        console_write(tool_outcome_name(rec[i].outcome));
        console_set_color(VGA_LIGHT_GREY);
        if (rec[i].args[0]) { console_write("  "); console_write(rec[i].args); }
        console_putc('\n');
    }
}
