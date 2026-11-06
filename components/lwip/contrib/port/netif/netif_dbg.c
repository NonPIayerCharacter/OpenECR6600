#include <string.h>

#include "netif_dbg.h"
#include "lwip/pbuf.h"
#include "lwip/ip4.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "oshal.h"
#include "lwip/ip.h"

/* 过滤阶段只保留匹配和摘要打印需要的最小字段。 */
typedef struct {
    u32_t src_ip;
    u32_t dst_ip;
    u16_t src_port;
    u16_t dst_port;
    u32_t tcp_seq;
    u32_t tcp_ack;
    u8_t tcp_flags;
    u16_t eth_type;
    u8_t ip_proto;
    u8_t has_ip;
    u8_t has_port;
    u8_t has_tcp_seqack;
} netdbg_packet_meta_t;

netdbg_info_t g_ipdbg;

static void netdbg_packet_rule_read(netdbg_packet_t *rule)
{
    unsigned int flags;

    if (rule == NULL) {
        return;
    }

    /* 先做规则快照，避免收包路径读到半更新配置。 */
    flags = system_irq_save();
    memcpy(rule, &g_ipdbg.packet, sizeof(*rule));
    system_irq_restore(flags);
}

static int netdbg_packet_rule_check(const netdbg_packet_t *rule)
{
    if (rule == NULL) {
        return -1;
    }

    /* 当前只支持头文件里定义的这些基础匹配位。 */
    if (rule->mask & ~(NETDBG_MASK_IPADDR | NETDBG_MASK_LENGTH | NETDBG_MASK_PROTCL |
                       NETDBG_MASK_SRCPORT | NETDBG_MASK_DSTPORT | NETDBG_MASK_DIR |
                       NETDBG_MASK_PORT)) {
        return -1;
    }

    if ((rule->mask & NETDBG_MASK_DIR) && rule->dir > NETDBG_PACKET_DIR_BOTH) {
        return -1;
    }

    return 0;
}

static int netdbg_packet_copy(const struct pbuf *packet, void *buf, u16_t len, u16_t offset)
{
    if (packet == NULL || buf == NULL) {
        return -1;
    }

    return (pbuf_copy_partial(packet, buf, len, offset) == len) ? 0 : -1;
}

static void netdbg_packet_tcp_record(const netdbg_packet_meta_t *meta, unsigned char actual_dir)
{
    netdbg_tcp_dir_t *record = NULL;
    unsigned int flags;

    if (meta == NULL || meta->has_tcp_seqack == 0) {
        return;
    }

    if (actual_dir == NETDBG_PACKET_DIR_TX) {
        record = &g_ipdbg.tcp.tx;
    } else if (actual_dir == NETDBG_PACKET_DIR_RX) {
        record = &g_ipdbg.tcp.rx;
    } else {
        return;
    }

    flags = system_irq_save();
    /* flags 和 last 记录最近一次命中的 TCP 包，便于看最近连接状态。 */
    record->flags = meta->tcp_flags;
    record->last.seqNum = meta->tcp_seq;
    record->last.ackNum = meta->tcp_ack;

    /* max 只保留更大的 seq/ack，避免重传小号覆盖累计推进位置。 */
    if (meta->tcp_seq > record->max.seqNum) {
        record->max.seqNum = meta->tcp_seq;
    }
    if (meta->tcp_ack > record->max.ackNum) {
        record->max.ackNum = meta->tcp_ack;
    }
    system_irq_restore(flags);
}

static int netdbg_packet_parse(struct pbuf *packet, netdbg_packet_meta_t *meta)
{
    struct eth_hdr ethhdr;
    u16_t offset = 0;

    if (packet == NULL || meta == NULL || packet->tot_len < SIZEOF_ETH_HDR) {
        return -1;
    }

    memset(meta, 0, sizeof(*meta));

    if (netdbg_packet_copy(packet, &ethhdr, sizeof(ethhdr), 0) != 0) {
        return -1;
    }

    /* 入口是二层帧，先解 Ethernet，再继续解 VLAN/IPv4/ARP。 */
    meta->eth_type = lwip_ntohs(ethhdr.type);
    offset = SIZEOF_ETH_HDR;

    if (meta->eth_type == ETHTYPE_VLAN) {
        struct eth_vlan_hdr vlanhdr;

        if ((u32_t)offset + sizeof(vlanhdr) > packet->tot_len) {
            return -1;
        }

        if (netdbg_packet_copy(packet, &vlanhdr, sizeof(vlanhdr), offset) != 0) {
            return -1;
        }

        /* VLAN 后面的 tpid 才是真正的上层协议类型。 */
        meta->eth_type = lwip_ntohs(vlanhdr.tpid);
        offset += SIZEOF_VLAN_HDR;
    }

    if (meta->eth_type == ETHTYPE_IP) {
        struct ip_hdr iphdr;
        u16_t ip_hlen;

        if ((u32_t)offset + sizeof(iphdr) > packet->tot_len) {
            return -1;
        }

        if (netdbg_packet_copy(packet, &iphdr, sizeof(iphdr), offset) != 0) {
            return -1;
        }

        if (IPH_V(&iphdr) != 4) {
            return 0;
        }

        ip_hlen = IPH_HL_BYTES(&iphdr);
        if (ip_hlen < sizeof(struct ip_hdr) || (u32_t)offset + ip_hlen > packet->tot_len) {
            return -1;
        }

        meta->has_ip = 1;
        meta->ip_proto = IPH_PROTO(&iphdr);
        meta->src_ip = ip4_addr_get_u32(&iphdr.src);
        meta->dst_ip = ip4_addr_get_u32(&iphdr.dest);
        offset += ip_hlen;

        if (meta->ip_proto == IP_PROTO_UDP) {
            struct udp_hdr udphdr;

            if ((u32_t)offset + sizeof(udphdr) > packet->tot_len) {
                return 0;
            }

            if (netdbg_packet_copy(packet, &udphdr, sizeof(udphdr), offset) != 0) {
                return -1;
            }

            meta->has_port = 1;
            meta->src_port = lwip_ntohs(udphdr.src);
            meta->dst_port = lwip_ntohs(udphdr.dest);
        } else if (meta->ip_proto == IP_PROTO_TCP) {
            struct tcp_hdr tcphdr;

            if ((u32_t)offset + sizeof(tcphdr) > packet->tot_len) {
                return 0;
            }

            if (netdbg_packet_copy(packet, &tcphdr, sizeof(tcphdr), offset) != 0) {
                return -1;
            }

            meta->has_port = 1;
            meta->src_port = lwip_ntohs(tcphdr.src);
            meta->dst_port = lwip_ntohs(tcphdr.dest);
            meta->has_tcp_seqack = 1;
            meta->tcp_seq = lwip_ntohl(tcphdr.seqno);
            meta->tcp_ack = lwip_ntohl(tcphdr.ackno);
            meta->tcp_flags = TCPH_FLAGS(&tcphdr);
        }
    } else if (meta->eth_type == ETHTYPE_ARP) {
        struct etharp_hdr arphdr;

        if ((u32_t)offset + sizeof(arphdr) > packet->tot_len) {
            return -1;
        }

        if (netdbg_packet_copy(packet, &arphdr, sizeof(arphdr), offset) != 0) {
            return -1;
        }

        meta->has_ip = 1;
        meta->src_ip = arphdr.sipaddr.addrw[0] | ((u32_t)arphdr.sipaddr.addrw[1] << 16);
        meta->dst_ip = arphdr.dipaddr.addrw[0] | ((u32_t)arphdr.dipaddr.addrw[1] << 16);
    }

    return 0;
}

static int netdbg_packet_ip_match(u32_t cfg_ip, u32_t packet_ip)
{
    /* 兼容规则里传主机序或网络序两种 IPv4 值。 */
    return (cfg_ip == packet_ip) || (cfg_ip == lwip_htonl(packet_ip));
}

static int netdbg_packet_dump_enabled(const netdbg_packet_t *rule)
{
    if (rule == NULL) {
        return 0;
    }

    /* len=0 且显式带 LENGTH 掩码时，只记录命中信息，不打印包内容。 */
    if ((rule->mask & NETDBG_MASK_LENGTH) && rule->len == 0) {
        return 0;
    }

    return 1;
}

static int netdbg_packet_dump_with_rule(struct pbuf *packet, const netdbg_packet_t *rule, unsigned char actual_dir)
{
    netdbg_packet_meta_t meta;
    ip4_addr_t src_addr;
    ip4_addr_t dst_addr;
    u16_t dump_len;
    u16_t offset;
    int parsed = -1;

    if (packet == NULL || rule == NULL) {
        return -1;
    }

    dump_len = packet->tot_len;
    /* LENGTH 只控制打印长度，不参与规则匹配；len=0 的禁打逻辑在过滤阶段处理。 */
    if ((rule->mask & NETDBG_MASK_LENGTH) &&
        rule->len > 0 &&
        rule->len < dump_len) {
        dump_len = (u16_t)rule->len;
    }

    parsed = netdbg_packet_parse(packet, &meta);
    if (parsed == 0 && meta.has_ip) {
        ip4_addr_set_u32(&src_addr, meta.src_ip);
        ip4_addr_set_u32(&dst_addr, meta.dst_ip);

        if (meta.has_port) {
            os_printf(LM_APP, LL_INFO,
                      "[netdbg] dir=%s eth=0x%04x proto=%u len=%u/%u %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                      (actual_dir == NETDBG_PACKET_DIR_TX) ? "tx" : "rx",
                      meta.eth_type, meta.ip_proto, dump_len, packet->tot_len,
                      (unsigned int)ip4_addr1_16(&src_addr), (unsigned int)ip4_addr2_16(&src_addr),
                      (unsigned int)ip4_addr3_16(&src_addr), (unsigned int)ip4_addr4_16(&src_addr),
                      meta.src_port,
                      (unsigned int)ip4_addr1_16(&dst_addr), (unsigned int)ip4_addr2_16(&dst_addr),
                      (unsigned int)ip4_addr3_16(&dst_addr), (unsigned int)ip4_addr4_16(&dst_addr),
                      meta.dst_port);
        } else {
            os_printf(LM_APP, LL_INFO,
                      "[netdbg] dir=%s eth=0x%04x proto=%u len=%u/%u %u.%u.%u.%u -> %u.%u.%u.%u\n",
                      (actual_dir == NETDBG_PACKET_DIR_TX) ? "tx" : "rx",
                      meta.eth_type, meta.ip_proto, dump_len, packet->tot_len,
                      (unsigned int)ip4_addr1_16(&src_addr), (unsigned int)ip4_addr2_16(&src_addr),
                      (unsigned int)ip4_addr3_16(&src_addr), (unsigned int)ip4_addr4_16(&src_addr),
                      (unsigned int)ip4_addr1_16(&dst_addr), (unsigned int)ip4_addr2_16(&dst_addr),
                      (unsigned int)ip4_addr3_16(&dst_addr), (unsigned int)ip4_addr4_16(&dst_addr));
        }
    } else {
        os_printf(LM_APP, LL_INFO, "[netdbg] dir=%s len=%u/%u\n",
                  (actual_dir == NETDBG_PACKET_DIR_TX) ? "tx" : "rx",
                  dump_len, packet->tot_len);
    }

    /* 逐行拷贝 pbuf 链，避免假设整包 payload 连续。 */
    for (offset = 0; offset < dump_len; offset += 16) {
        u8_t line_buf[16];
        u16_t line_len = (u16_t)((dump_len - offset) > sizeof(line_buf) ? sizeof(line_buf) : (dump_len - offset));
        u16_t idx;

        if (netdbg_packet_copy(packet, line_buf, line_len, offset) != 0) {
            return -1;
        }

        os_printf(LM_APP, LL_INFO, "[netdbg] %04x:", offset);
        for (idx = 0; idx < line_len; idx++) {
            os_printf(LM_APP, LL_INFO, " %02x", line_buf[idx]);
        }
        os_printf(LM_APP, LL_INFO, "\n");
    }

    return 0;
}

static int netdbg_packet_filter_with_dir(struct pbuf *packet, unsigned char actual_dir)
{
    netdbg_packet_meta_t meta;
    netdbg_packet_t rule;
    unsigned char mask;

    if (g_ipdbg.packet.mask == 0) {
        return 0;
    }

    if (packet == NULL) {
        return -1;
    }

    netdbg_packet_rule_read(&rule);
    mask = rule.mask;

    if ((mask & NETDBG_MASK_DIR) &&
        rule.dir != NETDBG_PACKET_DIR_BOTH &&
        rule.dir != actual_dir) {
        return 0;
    }

    /* 方向命中后，其余匹配位按“与”关系处理；LENGTH 不参与过滤。 */
    if ((mask & (NETDBG_MASK_IPADDR | NETDBG_MASK_PROTCL | NETDBG_MASK_SRCPORT |
                 NETDBG_MASK_DSTPORT | NETDBG_MASK_PORT)) == 0) {
        if (netdbg_packet_parse(packet, &meta) == 0) {
            /* 规则无需继续细分匹配时，TCP 包命中后同样记录 seq/ack。 */
            netdbg_packet_tcp_record(&meta, actual_dir);
        }
        if (!netdbg_packet_dump_enabled(&rule)) {
            return 1;
        }
        return (netdbg_packet_dump_with_rule(packet, &rule, actual_dir) == 0) ? 1 : -1;
    }

    if (netdbg_packet_parse(packet, &meta) != 0) {
        return 0;
    }

    if ((mask & NETDBG_MASK_IPADDR) &&
        (!meta.has_ip ||
         (!netdbg_packet_ip_match(rule.ipaddr, meta.src_ip) &&
          !netdbg_packet_ip_match(rule.ipaddr, meta.dst_ip)))) {
        return 0;
    }

    if ((mask & NETDBG_MASK_PROTCL) &&
        (meta.eth_type != ETHTYPE_IP || meta.ip_proto != rule.packeType)) {
        return 0;
    }

    if ((mask & NETDBG_MASK_SRCPORT) &&
        (!meta.has_port || meta.src_port != rule.srcPort)) {
        return 0;
    }

    if ((mask & NETDBG_MASK_DSTPORT) &&
        (!meta.has_port || meta.dst_port != rule.dstPort)) {
        return 0;
    }

    /* PORT 表示源/目的端口任意一侧命中即可，适合 DNS 这类双向业务流量。 */
    if ((mask & NETDBG_MASK_PORT) &&
        (!meta.has_port ||
         (meta.src_port != rule.port && meta.dst_port != rule.port))) {
        return 0;
    }

    /* TCP 包命中规则后，按收发方向分别记录最近一次匹配到的最大 seq/ack。 */
    netdbg_packet_tcp_record(&meta, actual_dir);

    if (!netdbg_packet_dump_enabled(&rule)) {
        return 1;
    }

    return (netdbg_packet_dump_with_rule(packet, &rule, actual_dir) == 0) ? 1 : -1;
}

int netdbg_packet_rule_set(const netdbg_packet_t *rule)
{
    netdbg_packet_t local_rule;
    unsigned int flags;

    if (netdbg_packet_rule_check(rule) != 0) {
        return -1;
    }

    memcpy(&local_rule, rule, sizeof(local_rule));
    local_rule.resv = 0;

    /* 通过局部副本一次性更新全局规则，减少关中断窗口。 */
    flags = system_irq_save();
    memcpy(&g_ipdbg.packet, &local_rule, sizeof(local_rule));
    memset(&g_ipdbg.tcp, 0, sizeof(g_ipdbg.tcp));
    system_irq_restore(flags);

    return 0;
}

int netdbg_packet_rule_get(netdbg_packet_t *rule)
{
    if (rule == NULL) {
        return -1;
    }

    netdbg_packet_rule_read(rule);

    return 0;
}

void netdbg_packet_rule_clear(void)
{
    netdbg_packet_t rule;
    unsigned int flags;

    /* 清空后 mask=0，默认不匹配也不打印任何数据包。 */
    memset(&rule, 0, sizeof(rule));

    flags = system_irq_save();
    memcpy(&g_ipdbg.packet, &rule, sizeof(rule));
    memset(&g_ipdbg.tcp, 0, sizeof(g_ipdbg.tcp));
    system_irq_restore(flags);
}

int netdbg_packet_filter_rx(struct pbuf *packet)
{
    return netdbg_packet_filter_with_dir(packet, NETDBG_PACKET_DIR_RX);
}

int netdbg_packet_filter_tx(struct pbuf *packet)
{
    return netdbg_packet_filter_with_dir(packet, NETDBG_PACKET_DIR_TX);
}
