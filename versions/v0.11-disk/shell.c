#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "pit.h"
#include "boot_info.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "memtest.h"
#include "cpu.h"
#include "floattest.h"
#include "time.h"
#include "disktest.h"

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
    console_write("zeroOS kernel v0.11 (x86, 32-bit protected mode, paging, SSE2, ATA PIO)\n");
}

static void cmd_uptime(const char *args) {
    unsigned int ms = time_ms();
    (void) args;
    console_write("uptime: ");
    console_write_dec(ms / 1000);
    console_putc('.');
    console_putc((char) ('0' + (ms % 1000) / 100));
    console_putc((char) ('0' + (ms % 100) / 10));
    console_write(" s (clock: ");
    console_write(time_source());
    /* 也列出时钟中断的次数：满负荷时它会比实际时间少（丢中断），可以和上面的时间对比 */
    console_write("; ");
    console_write_dec(pit_ticks());
    console_write(" timer interrupts)\n");
}

static const char *e820_type_name(unsigned int type) {
    switch (type) {
    case 1:  return "usable";
    case 2:  return "reserved";
    case 3:  return "ACPI reclaimable";
    case 4:  return "ACPI NVS";
    case 5:  return "bad memory";
    default: return "unknown";
    }
}

extern char __kernel_end[];   /* link.ld 导出：内核（含 .bss）在内存里的末端 */

static void cmd_mem(const char *args) {
    const struct e820_entry *map = (const struct e820_entry *) E820_MAP_ADDR;
    unsigned int count = *(const unsigned short *) E820_COUNT_ADDR, i;
    struct heap_stats hs;
    (void) args;

    console_write("BIOS memory map (E820):\n");
    for (i = 0; i < count && i < E820_MAX; i++) {
        unsigned long long len = (unsigned long long) map[i].len_hi << 32 | map[i].len_lo;
        unsigned long long end = ((unsigned long long) map[i].base_hi << 32 | map[i].base_lo) + len;
        console_write("  ");
        if (map[i].base_hi) {   /* 4GB 以上：32 位的 zeroOS 用不到，只报大小 */
            console_write("above 4GB, ");
            console_write_dec((unsigned int) (len >> 20));
            console_write(" MB  ");
            console_write(e820_type_name(map[i].type));
            console_write(" (not used by 32-bit zeroOS)\n");
            continue;
        }
        console_write_hex(map[i].base_lo);
        console_write(" - ");
        console_write_hex(end > 0xFFFFFFFFULL ? 0xFFFFFFFF : (unsigned int) end);
        console_write("  ");
        console_write(e820_type_name(map[i].type));
        console_putc('\n');
    }

    console_write("pages:  ");
    console_write_dec(pmm_free_pages());
    console_write(" free / ");
    console_write_dec(pmm_total_pages());
    console_write(" usable (");
    console_write_dec(pmm_free_pages() * 4);
    console_write(" KB free)\n");

    console_write("kernel: ");
    console_write_hex(0x100000);
    console_write(" - ");
    console_write_hex((unsigned int) __kernel_end);
    console_write(", page bitmap up to ");
    console_write_hex(pmm_bitmap_end());
    console_putc('\n');

    console_write("paging: on, identity-mapped up to ");
    console_write_hex(pmm_max_addr());
    console_write(", ");
    console_write_dec(paging_table_count());
    console_write(" page tables\n");

    heap_get_stats(&hs);
    console_write("heap:   ");
    console_write_hex(HEAP_START);
    console_write(", ");
    console_write_dec(hs.mapped / 1024);
    console_write(" KB mapped, ");
    console_write_dec(hs.used);
    console_write(" bytes in use, ");
    console_write_dec(hs.blocks);
    console_write(" blocks\n");
}

static void cmd_memtest(const char *args) {
    (void) args;
    memtest_run();
}

static void cmd_cpu(const char *args) {
    const struct cpu_info *c = cpu_get_info();
    (void) args;
    console_write("vendor: ");
    console_write(c->vendor);
    console_write("\nmodel:  ");
    console_write(c->brand[0] ? c->brand : "(not reported)");
    console_write("\nfeatures:");
    if (c->has_fpu)  console_write(" fpu");
    if (c->has_fxsr) console_write(" fxsr");
    if (c->has_sse)  console_write(" sse");
    if (c->has_sse2) console_write(" sse2");
    if (c->has_sse3) console_write(" sse3");
    if (c->has_tsc)  console_write(c->invariant_tsc ? " tsc(invariant)" : " tsc");
    console_write(c->float_enabled ? "\nfloating point: enabled (x87 + SSE2)\n"
                                   : "\nfloating point: disabled (needs fpu, fxsr, sse, sse2)\n");
    console_write("clock: ");
    if (time_tsc_khz()) {
        console_write("TSC at ");
        console_write_dec(time_tsc_khz() / 1000);
        console_write(" MHz\n");
    } else {
        console_write("counting timer interrupts (no TSC)\n");
    }
}

static void cmd_mathtest(const char *args) {
    (void) args;
    mathtest_run();
}

static void cmd_bench(const char *args) {
    (void) args;
    bench_run();
}

static const struct shell_command commands[] = {
    { "help",    "list commands",                    cmd_help },
    { "clear",   "clear the screen",                 cmd_clear },
    { "echo",    "print the rest of the line",       cmd_echo },
    { "uptime",  "time since boot",                  cmd_uptime },
    { "mem",     "memory map, free pages, heap",     cmd_mem },
    { "memtest", "self-test pages, paging and heap", cmd_memtest },
    { "cpu",     "processor model and features",     cmd_cpu },
    { "mathtest","self-test floating point and math",cmd_mathtest },
    { "bench",   "measure matrix speed, estimate LLM speed", cmd_bench },
    { "disks",   "list disks",                       cmd_disks },
    { "diskread","diskread <disk> <sector>: show a sector", cmd_diskread },
    { "diskcheck","diskcheck <disk> <first> <count>: verify test pattern", cmd_diskcheck },
    { "diskbench","diskbench <disk> <MB>: measure read speed", cmd_diskbench },
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
