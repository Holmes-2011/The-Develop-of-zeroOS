#ifndef BLOCKDEV_H
#define BLOCKDEV_H

/* 块设备接口（v0.11）：所有硬盘驱动都实现它，上层（模型加载、以后的文件系统）只认它。

     上层代码 ──→ blockdev_read(dev, 扇区号, 扇区数, 缓冲区)
                        │
              ┌─────────┼─────────────┐
           ATA PIO     AHCI（以后）   NVMe（以后）

   以后加新的硬盘驱动，只要实现 read，再 blockdev_register 一下，上层一行不用改。

   为了让以后的 DMA 驱动（AHCI / NVMe）也能直接套进来，接口按它们的需要设计：
     * 一次可以读很多个扇区（驱动自己按硬件上限分批）
     * 调用方的缓冲区按 4KB 对齐（kmalloc 不保证，用 blockdev_alloc_buffer 申请）
   ATA PIO 用不上这两点，但现在就这样约定，以后换驱动时上层不用改 */

#define SECTOR_SIZE   512
#define BLOCKDEV_MAX  8

struct block_device {
    char               name[8];        /* "ata0"、"ata1"…… */
    char               model[41];      /* 硬盘自己报的型号 */
    unsigned long long sectors;        /* 一共多少个扇区 */
    int                lba48;          /* 支持 48 位寻址（能访问 128GB 以上） */

    /* 读 count 个扇区到 buf（至少 count × 512 字节）。成功返回 0，失败返回负数 */
    int (*read)(struct block_device *dev, unsigned long long lba, unsigned int count, void *buf);
    /* 写：以后再做。写盘属于"破坏性"操作，届时要过 Agent 设计里的权限检查 */

    void *driver_data;
};

void                 blockdev_register(struct block_device *dev);
int                  blockdev_count(void);
struct block_device *blockdev_get(int index);
struct block_device *blockdev_find(const char *name);

/* 带检查的读：越界、count 为 0 都直接返回错误，不交给驱动 */
int blockdev_read(struct block_device *dev, unsigned long long lba, unsigned int count, void *buf);

/* 申请 / 释放读盘缓冲区：按 4KB 对齐（见上面的约定） */
void *blockdev_alloc_buffer(unsigned int sectors);
void  blockdev_free_buffer(void *buf);

#endif
