#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

int  virtio_net_init(void);
int  virtio_net_ready(void);
const unsigned char *virtio_net_mac(void);
int  virtio_net_send(const void *frame, unsigned int len);
int  virtio_net_recv(void *buf, unsigned int cap, unsigned int *out_len);

#endif
