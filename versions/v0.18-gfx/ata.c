#include "ata.h"
#include "blockdev.h"
#include "io.h"
#include "time.h"
#include "kstring.h"

/* 端口：命令块寄存器（base + 0 ~ 7）和控制寄存器（ctrl） */
#define REG_DATA     0
#define REG_ERROR    1
#define REG_SECCOUNT 2
#define REG_LBA0     3
#define REG_LBA1     4
#define REG_LBA2     5
#define REG_DRIVE    6
#define REG_STATUS   7   /* 读 */
#define REG_COMMAND  7   /* 写 */

#define ST_BSY  0x80     /* 忙：这时其他位都不可信 */
#define ST_DRDY 0x40
#define ST_DF   0x20     /* 设备故障 */
#define ST_DRQ  0x08     /* 可以传数据了 */
#define ST_ERR  0x01

#define CMD_READ_PIO      0x20
#define CMD_READ_PIO_EXT  0x24   /* 48 位寻址版 */
#define CMD_IDENTIFY      0xEC

#define TIMEOUT_MS 2000

struct ata_drive {
    unsigned short base, ctrl;
    int slave;
    struct block_device dev;
};

static struct ata_drive drives[4];
static int found;

/* 选完盘、发完命令后要等 400 纳秒状态才可靠：读 4 次备用状态寄存器（每次约 100 纳秒） */
static void delay400(struct ata_drive *d) {
    inb(d->ctrl); inb(d->ctrl); inb(d->ctrl); inb(d->ctrl);
}

/* 等 BSY 清零。返回最后的状态；超时返回 -1 */
static int wait_not_busy(struct ata_drive *d) {
    unsigned int start = time_ms();
    unsigned char st;
    while ((st = inb(d->ctrl)) & ST_BSY) {
        if (time_ms() - start > TIMEOUT_MS) return -1;
    }
    return st;
}

/* 等可以传数据（DRQ）。出错（ERR / DF）或超时返回负数 */
static int wait_drq(struct ata_drive *d) {
    unsigned int start = time_ms();
    unsigned char st;
    for (;;) {
        st = inb(d->ctrl);
        if (!(st & ST_BSY)) {
            if (st & (ST_ERR | ST_DF)) return -2;
            if (st & ST_DRQ) return 0;
        }
        if (time_ms() - start > TIMEOUT_MS) return -1;
    }
}

static void insw(unsigned short port, void *buf, unsigned int words) {
    __asm__ __volatile__("cld; rep insw" : "+D"(buf), "+c"(words) : "d"(port) : "memory");
}

/* 型号字符串：每个 16 位字里两个字节是反过来存的，末尾用空格填充 */
static void copy_model(char *dst, const unsigned short *id) {
    int i;
    for (i = 0; i < 20; i++) {
        dst[i * 2]     = (char) (id[27 + i] >> 8);
        dst[i * 2 + 1] = (char) (id[27 + i] & 0xFF);
    }
    dst[40] = '\0';
    for (i = 39; i >= 0 && dst[i] == ' '; i--) dst[i] = '\0';
}

static int ata_read(struct block_device *dev, unsigned long long lba, unsigned int count, void *buf) {
    struct ata_drive *d = dev->driver_data;
    unsigned char *p = buf;

    while (count) {
        /* LBA48 一条命令最多 65536 个扇区，LBA28 最多 256 个 */
        unsigned int n = count;
        unsigned int i;
        int use48 = dev->lba48 && (lba + n > 0x0FFFFFFFULL || n > 256);
        if (!use48 && n > 256) n = 256;
        if (use48 && n > 65536) n = 65536;

        if (wait_not_busy(d) < 0) return -1;
        if (use48) {
            outb(d->base + REG_DRIVE, (unsigned char) (0x40 | (d->slave << 4)));
            delay400(d);
            /* 48 位：每个寄存器写两次，先高字节、后低字节 */
            outb(d->base + REG_SECCOUNT, (unsigned char) ((n >> 8) & 0xFF));   /* 65536 写成 0 */
            outb(d->base + REG_LBA0, (unsigned char) (lba >> 24));
            outb(d->base + REG_LBA1, (unsigned char) (lba >> 32));
            outb(d->base + REG_LBA2, (unsigned char) (lba >> 40));
            outb(d->base + REG_SECCOUNT, (unsigned char) (n & 0xFF));
            outb(d->base + REG_LBA0, (unsigned char) lba);
            outb(d->base + REG_LBA1, (unsigned char) (lba >> 8));
            outb(d->base + REG_LBA2, (unsigned char) (lba >> 16));
            outb(d->base + REG_COMMAND, CMD_READ_PIO_EXT);
        } else {
            outb(d->base + REG_DRIVE, (unsigned char) (0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F)));
            delay400(d);
            outb(d->base + REG_SECCOUNT, (unsigned char) (n & 0xFF));           /* 256 写成 0 */
            outb(d->base + REG_LBA0, (unsigned char) lba);
            outb(d->base + REG_LBA1, (unsigned char) (lba >> 8));
            outb(d->base + REG_LBA2, (unsigned char) (lba >> 16));
            outb(d->base + REG_COMMAND, CMD_READ_PIO);
        }

        /* 每个扇区：等硬盘准备好，再从数据端口搬 256 个 16 位字 */
        for (i = 0; i < n; i++) {
            delay400(d);
            if (wait_drq(d) < 0) return -2;
            insw(d->base + REG_DATA, p, SECTOR_SIZE / 2);
            p += SECTOR_SIZE;
        }
        lba += n;
        count -= n;
    }
    return 0;
}

/* 发 IDENTIFY，问这个位置上有没有硬盘、多大、什么型号 */
static int probe(struct ata_drive *d) {
    static unsigned short id[256];
    unsigned char st;

    st = inb(d->base + REG_STATUS);
    if (st == 0xFF) return 0;                  /* 总线"悬空"：这个通道上根本没有控制器 */

    outb(d->ctrl, 0x02);                       /* nIEN：不要中断，我们轮询 */
    outb(d->base + REG_DRIVE, (unsigned char) (0xA0 | (d->slave << 4)));
    delay400(d);
    outb(d->base + REG_SECCOUNT, 0);
    outb(d->base + REG_LBA0, 0);
    outb(d->base + REG_LBA1, 0);
    outb(d->base + REG_LBA2, 0);
    outb(d->base + REG_COMMAND, CMD_IDENTIFY);
    delay400(d);

    if (inb(d->base + REG_STATUS) == 0) return 0;          /* 状态为 0：这个位置没接设备 */
    if (wait_not_busy(d) < 0) return 0;
    if (inb(d->base + REG_LBA1) || inb(d->base + REG_LBA2)) return 0;   /* 光驱（ATAPI）或别的东西 */
    if (wait_drq(d) < 0) return 0;
    insw(d->base + REG_DATA, id, 256);

    copy_model(d->dev.model, id);
    d->dev.lba48 = (id[83] >> 10) & 1;
    if (d->dev.lba48) {
        d->dev.sectors = (unsigned long long) id[100] | ((unsigned long long) id[101] << 16)
                       | ((unsigned long long) id[102] << 32) | ((unsigned long long) id[103] << 48);
    } else {
        d->dev.sectors = (unsigned long long) id[60] | ((unsigned long long) id[61] << 16);
    }
    return d->dev.sectors != 0;
}

int ata_init(void) {
    static const unsigned short bases[2] = { 0x1F0, 0x170 }, ctrls[2] = { 0x3F6, 0x376 };
    int ch, sl;
    for (ch = 0; ch < 2; ch++) {
        for (sl = 0; sl < 2; sl++) {
            struct ata_drive *d = &drives[found];
            d->base = bases[ch];
            d->ctrl = ctrls[ch];
            d->slave = sl;
            if (!probe(d)) continue;
            memcpy(d->dev.name, "ata0", 5);
            d->dev.name[3] = (char) ('0' + found);
            d->dev.read = ata_read;
            d->dev.driver_data = d;
            blockdev_register(&d->dev);
            found++;
        }
    }
    return found;
}
