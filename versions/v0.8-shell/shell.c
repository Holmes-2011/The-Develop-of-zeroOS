#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "pit.h"
#include "io.h"

#define LINE_MAX 71   /* 一行 80 列 = 提示符 "zeroOS> " 8 列 + 输入 71 列 + 光标 1 列，长命令不会折行 */

struct shell_command {
    const char *name;
    const char *help;
    void (*run)(const char *args);
};

/* ---------------- 各个命令 ---------------- */

static void cmd_help(const char *args);

static void cmd_clear(const char *args) {
    (void) args;
    vga_clear();
}

static void cmd_echo(const char *args) {
    console_write(args);
    console_putc('\n');
}

static void cmd_version(const char *args) {
    (void) args;
    console_write("zeroOS kernel v0.8 (x86, 32-bit protected mode)\n");
}

static void cmd_uptime(const char *args) {
    unsigned int t = pit_ticks();   /* PIT 100 Hz：1 tick = 10 毫秒 */
    (void) args;
    console_write("uptime: ");
    console_write_dec(t / 100);
    console_putc('.');
    console_putc((char) ('0' + (t % 100) / 10));
    console_putc((char) ('0' + t % 10));
    console_write(" s (");
    console_write_dec(t);
    console_write(" ticks)\n");
}

/* 读 CMOS（主板上那块带电池的小存储）里的一个字节 */
static unsigned char cmos_read(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

extern char __kernel_end[];   /* link.ld 导出：内核（含 .bss）在内存里的末端 */

static void cmd_mem(const char *args) {
    unsigned int ext_kb   = cmos_read(0x30) | (cmos_read(0x31) << 8);   /* 1MB 以上的内存，KB，最多约 64MB */
    unsigned int above16m = cmos_read(0x34) | (cmos_read(0x35) << 8);   /* 16MB 以上的内存，单位 64KB */
    unsigned int total_kb = above16m ? 16 * 1024 + above16m * 64 : 1024 + ext_kb;
    unsigned int kernel_end = (unsigned int) __kernel_end;
    (void) args;

    console_write("RAM:    ");
    console_write_dec(total_kb);
    console_write(" KB (");
    console_write_dec(total_kb / 1024);
    console_write(" MB, from CMOS)\n");

    console_write("kernel: ");
    console_write_hex(0x1000);
    console_write(" - ");
    console_write_hex(kernel_end);
    console_write(" (");
    console_write_dec(kernel_end - 0x1000);
    console_write(" bytes incl. .bss, limit 0x00007C00)\n");

    console_write("stack:  grows down from 0x00090000\n");
    console_write("note:   no memory manager yet (planned for v0.9)\n");
}

static const struct shell_command commands[] = {
    { "help",    "list commands",                    cmd_help },
    { "clear",   "clear the screen",                 cmd_clear },
    { "echo",    "print the rest of the line",       cmd_echo },
    { "uptime",  "time since boot",                  cmd_uptime },
    { "mem",     "memory size and kernel layout",    cmd_mem },
    { "version", "kernel version",                   cmd_version },
};
#define NUM_COMMANDS ((int) (sizeof(commands) / sizeof(commands[0])))

static void cmd_help(const char *args) {
    int i;
    (void) args;
    for (i = 0; i < NUM_COMMANDS; i++) {
        const char *p = commands[i].name;
        int len = 0;
        console_write("  ");
        console_write(p);
        while (p[len]) len++;
        while (len++ < 10) console_putc(' ');   /* 名字补齐到 10 列，说明对齐 */
        console_write(commands[i].help);
        console_putc('\n');
    }
}

/* ---------------- 解析 + 执行 ---------------- */

/* 比较命令名：name 必须和 line 开头的那个词完全一样（不能只是前缀） */
static int word_equals(const char *word, int word_len, const char *name) {
    int i;
    for (i = 0; i < word_len; i++) {
        if (name[i] != word[i]) return 0;
    }
    return name[word_len] == '\0';
}

static void execute(const char *line) {
    const char *word, *args;
    int word_len = 0, i;

    while (*line == ' ') line++;
    if (*line == '\0') return;                 /* 空行：什么都不做 */

    word = line;
    while (word[word_len] && word[word_len] != ' ') word_len++;
    args = word + word_len;
    while (*args == ' ') args++;

    for (i = 0; i < NUM_COMMANDS; i++) {
        if (word_equals(word, word_len, commands[i].name)) {
            commands[i].run(args);
            return;
        }
    }

    console_set_color(VGA_LIGHT_RED);
    console_write("unknown command: ");
    for (i = 0; i < word_len; i++) console_putc(word[i]);
    console_set_color(VGA_LIGHT_GREY);
    console_write(" (type 'help')\n");
}

static void prompt(void) {
    console_set_color(VGA_LIGHT_GREEN);
    console_write("zeroOS> ");
    console_set_color(VGA_LIGHT_GREY);
}

/* 等下一个字符。关中断后再检查缓冲区：如果检查完、hlt 之前恰好来了中断，
   CPU 就会带着"已经有字符"的状态睡下去，要等下一次中断才醒。
   "sti; hlt" 连在一起没有这个问题：sti 之后的那一条指令执行完才会响应中断 */
static int wait_char(void) {
    while (1) {
        int c;
        __asm__ __volatile__("cli");
        c = keyboard_getchar();
        if (c >= 0) {
            __asm__ __volatile__("sti");
            return c;
        }
        __asm__ __volatile__("sti; hlt");
    }
}

void shell_run(void) {
    char line[LINE_MAX + 1];
    int len = 0;

    prompt();
    while (1) {
        int c = wait_char();

        if (c == '\n') {
            console_putc('\n');
            line[len] = '\0';
            execute(line);
            len = 0;
            prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_write("\b \b");   /* 退一格、用空格盖掉、再退一格 */
            }
        } else if (c >= ' ' && c <= '~' && len < LINE_MAX) {
            line[len++] = (char) c;
            console_putc((char) c);
        }
    }
}
