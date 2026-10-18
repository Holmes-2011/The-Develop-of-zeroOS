#include "disktest.h"
#include "blockdev.h"
#include "console.h"
#include "time.h"
#include "kstring.h"

/* ---------------- 参数解析 ---------------- */

/* 取下一个空格分隔的词，复制到 out（最多 cap-1 个字符），返回剩下的部分 */
static const char *next_word(const char *p, char *out, int cap) {
    int n = 0;
    while (*p == ' ') p++;
    while (*p && *p != ' ') {
        if (n < cap - 1) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return p;
}

/* 十进制数；不是数字返回 -1 */
static int parse_u64(const char *s, unsigned long long *v) {
    *v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        *v = *v * 10 + (unsigned long long) (*s - '0');
    }
    return 0;
}

static struct block_device *disk_arg(const char *name) {
    struct block_device *d = blockdev_find(name);
    if (!d) {
        console_write("no such disk: ");
        console_write(name[0] ? name : "(none)");
        console_write(" (see 'disks')\n");
    }
    return d;
}

/* 64 位数打印：核心代码不能用 64 位除法，所以只打印 ≤ 4294967295 的部分，更大的用 MB 显示 */
static void write_sectors_size(unsigned long long sectors) {
    if (sectors < (1ULL << 21)) {                    /* 小于 1GB：显示 KB / MB */
        unsigned int kb = (unsigned int) (sectors >> 1);
        if (kb < 4096) { console_write_dec(kb); console_write(" KB"); }
        else           { console_write_dec(kb >> 10); console_write(" MB"); }
    } else {
        console_write_dec((unsigned int) (sectors >> 21));   /* 2^21 扇区 = 1GB */
        console_write(" GB");
    }
}

/* ---------------- 测试图案（和 gen_testdisk.py 一致） ---------------- */

static void fill_pattern(unsigned long long lba, unsigned int *w) {
    unsigned int lo = (unsigned int) lba, hi = (unsigned int) (lba >> 32);
    unsigned int x = (lo ^ (hi * 0x9E3779B9U)) * 2654435761U + 1;
    int i;
    if (x == 0) x = 1;
    memcpy(w, "zeroOSdk", 8);
    w[2] = lo;
    w[3] = hi;
    for (i = 0; i < 124; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        w[4 + i] = x;
    }
}

/* ---------------- 命令 ---------------- */

void cmd_disks(const char *args) {
    int i;
    (void) args;
    if (blockdev_count() == 0) {
        console_write("no disks found (only ATA/IDE is supported for now; AHCI/NVMe come later)\n");
        return;
    }
    for (i = 0; i < blockdev_count(); i++) {
        struct block_device *d = blockdev_get(i);
        console_write(d->name);
        console_write(": ");
        write_sectors_size(d->sectors);
        console_write(d->lba48 ? ", LBA48, \"" : ", LBA28, \"");
        console_write(d->model);
        console_write("\"\n");
    }
}

void cmd_diskread(const char *args) {
    char name[8], num[24];
    unsigned long long lba;
    unsigned char *buf;
    struct block_device *d;
    int i, j, r;

    args = next_word(args, name, sizeof(name));
    next_word(args, num, sizeof(num));
    if (!(d = disk_arg(name))) return;
    if (parse_u64(num, &lba) < 0) { console_write("usage: diskread <disk> <sector>\n"); return; }
    buf = blockdev_alloc_buffer(1);
    if (!buf) { console_write("out of memory\n"); return; }

    r = blockdev_read(d, lba, 1, buf);
    if (r < 0) {
        console_write(r == -2 ? "sector out of range\n" : "read error\n");
    } else {
        for (i = 0; i < 64; i += 16) {   /* 前 64 字节：十六进制 + 可打印字符 */
            static const char hex[] = "0123456789ABCDEF";
            for (j = 0; j < 16; j++) {
                console_putc(hex[buf[i + j] >> 4]);
                console_putc(hex[buf[i + j] & 15]);
                console_putc(' ');
            }
            console_write(" ");
            for (j = 0; j < 16; j++) console_putc(buf[i + j] >= 32 && buf[i + j] < 127 ? (char) buf[i + j] : '.');
            console_putc('\n');
        }
        console_write("... last 2 bytes: ");
        console_write(buf[510] == 0x55 && buf[511] == 0xAA ? "55 AA (boot signature)\n" : "not a boot signature\n");
    }
    blockdev_free_buffer(buf);
}

#define CHUNK 2048  /* 每次读 2048 个扇区（1MB）：超过 LBA28 一条命令的 256 上限，顺带测到驱动的 LBA48 大块读和分批 */

void cmd_diskcheck(const char *args) {
    char name[8], a[24], b[24];
    unsigned long long start, count, done = 0, bad = 0, first_bad = 0;
    static unsigned int expect[128];
    unsigned char *buf;
    struct block_device *d;

    args = next_word(args, name, sizeof(name));
    args = next_word(args, a, sizeof(a));
    next_word(args, b, sizeof(b));
    if (!(d = disk_arg(name))) return;
    if (parse_u64(a, &start) < 0 || parse_u64(b, &count) < 0 || count == 0) {
        console_write("usage: diskcheck <disk> <first sector> <sector count>\n");
        return;
    }
    buf = blockdev_alloc_buffer(CHUNK);
    if (!buf) { console_write("out of memory\n"); return; }

    while (done < count) {
        unsigned int n = count - done > CHUNK ? CHUNK : (unsigned int) (count - done), i;
        if (blockdev_read(d, start + done, n, buf) < 0) {
            console_write("read error at sector ");
            console_write_dec((unsigned int) (start + done));
            console_putc('\n');
            blockdev_free_buffer(buf);
            return;
        }
        for (i = 0; i < n; i++) {
            fill_pattern(start + done + i, expect);
            if (memcmp(buf + i * SECTOR_SIZE, expect, SECTOR_SIZE) != 0) {
                if (!bad) first_bad = start + done + i;
                bad++;
            }
        }
        done += n;
    }
    blockdev_free_buffer(buf);

    console_write("diskcheck: ");
    console_write_dec((unsigned int) count);
    console_write(" sectors, ");
    console_write_dec((unsigned int) bad);
    console_write(" bad");
    if (bad) {
        console_write(" (first at sector ");
        console_write_dec((unsigned int) first_bad);
        console_putc(')');
    }
    console_putc('\n');
}

void cmd_diskbench(const char *args) {
    char name[8], a[24];
    unsigned long long mb, sectors, done = 0;
    unsigned int t0, ms, kbps;
    unsigned char *buf;
    struct block_device *d;

    args = next_word(args, name, sizeof(name));
    next_word(args, a, sizeof(a));
    if (!(d = disk_arg(name))) return;
    if (parse_u64(a, &mb) < 0 || mb == 0 || mb > 4096) { console_write("usage: diskbench <disk> <MB, 1..4096>\n"); return; }
    sectors = mb << 11;
    if (sectors > d->sectors) sectors = d->sectors;
    buf = blockdev_alloc_buffer(CHUNK);
    if (!buf) { console_write("out of memory\n"); return; }

    t0 = time_ms();
    while (done < sectors) {
        unsigned int n = sectors - done > CHUNK ? CHUNK : (unsigned int) (sectors - done);
        if (blockdev_read(d, done, n, buf) < 0) { console_write("read error\n"); blockdev_free_buffer(buf); return; }
        done += n;
    }
    ms = time_ms() - t0;
    blockdev_free_buffer(buf);
    if (ms == 0) ms = 1;
    /* KB/s = KB × 1000 ÷ 毫秒。最多 4GB = 4194304 KB，乘 1000 会超出 32 位，
       而核心代码不能做 64 位除法——拆成"整数部分 + 余数部分"，每一步都在 32 位以内 */
    {
        unsigned int kb = (unsigned int) (done >> 1);
        kbps = kb / ms * 1000 + kb % ms * 1000 / ms;
    }

    console_write("diskbench: read ");
    console_write_dec((unsigned int) (done >> 11));
    console_write(" MB in ");
    console_write_dec(ms);
    console_write(" ms = ");
    console_write_dec(kbps);
    console_write(" KB/s\n");
    console_write("  loading 1 GB would take about ");
    console_write_dec(1024 * 1024 / (kbps ? kbps : 1));
    console_write(" s, 1.5 GB about ");
    console_write_dec(1536 * 1024 / (kbps ? kbps : 1));
    console_write(" s, 3 GB about ");
    console_write_dec(3072 * 1024 / (kbps ? kbps : 1));
    console_write(" s\n");
}
