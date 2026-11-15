#include "pci.h"
#include "io.h"

static unsigned int cfg_addr(int bus, int dev, int fn, int off) {
    return 0x80000000U | ((unsigned int) bus << 16) | ((unsigned int) dev << 11) |
           ((unsigned int) fn << 8) | ((unsigned int) off & 0xFC);
}

unsigned int pci_read32(int bus, int dev, int fn, int off) {
    outl(0xCF8, cfg_addr(bus, dev, fn, off));
    return inl(0xCFC);
}

int pci_find_class(unsigned char cls, unsigned char sub, unsigned char prog,
                   int *bus, int *dev, int *fn) {
    int b, d, f;
    for (b = 0; b < 4; b++) {
        for (d = 0; d < 32; d++) {
            for (f = 0; f < 8; f++) {
                unsigned int id = pci_read32(b, d, f, 0);
                unsigned int cc;
                if (id == 0xFFFFFFFFU) continue;
                cc = pci_read32(b, d, f, 8) >> 8;
                if (((cc >> 16) & 0xFF) == cls &&
                    ((cc >> 8) & 0xFF) == sub &&
                    (cc & 0xFF) == prog) {
                    *bus = b; *dev = d; *fn = f;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int pci_find_id(unsigned short vendor, unsigned short device,
                int *bus, int *dev, int *fn) {
    int b, d, f;
    for (b = 0; b < 4; b++) {
        for (d = 0; d < 32; d++) {
            for (f = 0; f < 8; f++) {
                unsigned int id = pci_read32(b, d, f, 0);
                if (id == 0xFFFFFFFFU) continue;
                if ((id & 0xFFFF) == vendor && ((id >> 16) & 0xFFFF) == device) {
                    *bus = b; *dev = d; *fn = f;
                    return 1;
                }
            }
        }
    }
    return 0;
}

void pci_write16(int bus, int dev, int fn, int off, unsigned short val) {
    unsigned int a = cfg_addr(bus, dev, fn, off);
    outl(0xCF8, a);
    outl(0xCFC, (inl(0xCFC) & 0xFFFF0000U) | val);
}
