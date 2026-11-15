#include "blockdev.h"
#include "heap.h"
#include "kstring.h"

static struct block_device *devices[BLOCKDEV_MAX];
static int count;

void blockdev_register(struct block_device *dev) {
    if (count < BLOCKDEV_MAX) devices[count++] = dev;
}

int blockdev_count(void) { return count; }

struct block_device *blockdev_get(int index) {
    return (index >= 0 && index < count) ? devices[index] : 0;
}

struct block_device *blockdev_find(const char *name) {
    int i;
    for (i = 0; i < count; i++) {
        if (strlen(name) == strlen(devices[i]->name) && memcmp(name, devices[i]->name, strlen(name)) == 0) {
            return devices[i];
        }
    }
    return 0;
}

int blockdev_read(struct block_device *dev, unsigned long long lba, unsigned int n, void *buf) {
    if (!dev || !buf || n == 0) return -1;
    if (lba >= dev->sectors || n > dev->sectors - lba) return -2;   /* 越界 */
    return dev->read(dev, lba, n, buf);
}

/* kmalloc 只保证 8 字节对齐。多要 4KB，把返回的地址往上对齐，
   对齐前的原始指针存在对齐后地址的前 4 个字节里，释放时取回来 */
void *blockdev_alloc_buffer(unsigned int sectors) {
    char *raw = kmalloc(sectors * SECTOR_SIZE + 4096 + 4);
    unsigned int aligned;
    if (!raw) return 0;
    aligned = ((unsigned int) raw + 4 + 4095) & ~4095U;
    ((void **) aligned)[-1] = raw;
    return (void *) aligned;
}

void blockdev_free_buffer(void *buf) {
    if (buf) kfree(((void **) buf)[-1]);
}
