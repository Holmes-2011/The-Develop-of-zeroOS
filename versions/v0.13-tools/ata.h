#ifndef ATA_H
#define ATA_H

/* ATA（IDE）硬盘驱动，PIO 方式（v0.11，轻量路线）。
   PIO：CPU 通过 I/O 端口一次搬 2 字节，简单但慢；以后的 AHCI / NVMe 驱动用 DMA。

   探测两个通道（主 0x1F0、从 0x170）上的主盘 / 从盘，共 4 个位置；
   找到的硬盘注册成块设备 ata0、ata1……（见 blockdev.h）。
   只认硬盘：光驱（ATAPI）跳过。不用中断，轮询状态寄存器，每次等待都有超时。

   注意：QEMU 的 q35 机型、以及现代电脑的 SATA 控制器默认工作在 AHCI 模式，
   没有这些老式端口——那里这个驱动什么也找不到（不会卡住），要等以后的 AHCI 驱动 */

int ata_init(void);   /* 返回找到的硬盘数 */

#endif
