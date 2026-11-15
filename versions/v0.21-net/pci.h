#ifndef PCI_H
#define PCI_H

/* 最小 PCI 配置空间访问（v0.20）：只用它找 UHCI 控制器。 */

unsigned int pci_read32(int bus, int dev, int fn, int off);
void pci_write16(int bus, int dev, int fn, int off, unsigned short val);
int pci_find_class(unsigned char cls, unsigned char sub, unsigned char prog,
                   int *bus, int *dev, int *fn);
int pci_find_id(unsigned short vendor, unsigned short device,
                int *bus, int *dev, int *fn);

#endif
