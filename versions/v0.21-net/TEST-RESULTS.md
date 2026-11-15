# v0.21 测试记录

## 本版内容

virtio-net（legacy）+ ARP / IPv4 / ICMP / UDP / DNS；命令 `net`、`ping`、`dns`。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 全套（含网络） | ✅ **166/166** |
| 网络：`network ready (virtio-net` | ✅ |
| 网络：网关 ARP 解析 `link: up (gateway resolved)` | ✅ |
| 网络：`ping 10.0.2.2` 有回复 | ✅ |
| 无网卡启动 | ✅ 不阻塞，`net` 报无设备 |
| q35 | 未跑网络阶段（阶段 9 只在 pc 跑） |

### 验证方法

```
qemu ... -netdev user,id=n0 -device virtio-net-pci,netdev=n0
sendkey: net / ping 10.0.2.2
```

QEMU user-mode 网络固定 10.0.2.0/24：网关 10.0.2.2、DNS 10.0.2.3，
所以地址写死也能工作；换成 vmnet/bridged 网络需要 DHCP（未实现）。

## 踩坑

| 问题 | 修复 |
|---|---|
| legacy virtio queue size 只读（256） | 读 0x0C，按设备值算 desc/avail/used 偏移 |
| `hlt` 等待 ARP 时中断还没开 | `net_init()` 移到 `sti` 之后 |
| ARP 缓存命中时往 NULL 写 MAC | `arp_lookup` 判空 |

## 已知限制

- 只有 legacy virtio-net（I/O BAR）；没有 e1000、没有 modern MMIO。
- 没有 DHCP，IP/网关/DNS 写死。
- 没有 TCP/HTTP/TLS，不能下载网页。
- 没有 IPv6、没有分片重组（>1500 字节的 IP 包不支持）。

## 截图 / 输出

见 release notes 的 `net` / `ping` / `dns` 实测输出。
