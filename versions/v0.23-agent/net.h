#ifndef NET_H
#define NET_H

/* 最小网络栈（v0.21）：virtio-net + ARP/IPv4/ICMP/UDP/DNS。
   不实现 TCP、TLS、DHCP（地址写死成 QEMU user-mode 的 10.0.2.15/24）。 */

void net_init(void);
int  net_ready(void);
void net_poll(void);                      /* 处理收到的包（主循环 / 等待回复时调用） */

void net_info(void);                      /* net 命令：MAC / IP / 网关 */
int  net_ping(unsigned int ip, unsigned int *rtt_ms);
int  net_dns(const char *name, unsigned int *out_ip);

int  net_parse_ip(const char *s, unsigned int *out);   /* "10.0.2.2" → 整数 */

#endif
