#ifndef BOOT_INFO_H
#define BOOT_INFO_H

/* 引导扇区留给内核的信息。地址必须和 boot.asm 里的 E820_COUNT / E820_MAP 一致 */

#define E820_COUNT_ADDR 0x5000   /* 一个 16 位整数：BIOS 报了几条 */
#define E820_MAP_ADDR   0x5004   /* 每条 24 字节，最多 E820_MAX 条 */
#define E820_MAX        64

#define E820_USABLE 1             /* 只有类型 1 是可以随便用的内存 */

struct e820_entry {
    unsigned int base_lo, base_hi;     /* 起始地址（64 位，拆成两半） */
    unsigned int len_lo,  len_hi;      /* 长度 */
    unsigned int type;                 /* 1 可用；2 保留；3 ACPI 可回收；4 ACPI NVS；5 坏内存 */
    unsigned int acpi_ext;
} __attribute__((packed));

#endif
