#include <string.h>

#include "cli.h"
#include "oshal.h"
#include "netif/netif_dbg.h"
#include "lwip/ip4_addr.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/tcp.h"

/* lwip packet set 支持的关键字数量较少，保持简单线性解析即可。 */
typedef struct {
    const char *name;
    unsigned char value;
} cmd_lwip_name_map_t;

static const cmd_lwip_name_map_t g_lwip_proto_map[] = {
    {"icmp", IP_PROTO_ICMP},
    {"igmp", IP_PROTO_IGMP},
    {"tcp",  IP_PROTO_TCP},
    {"udp",  IP_PROTO_UDP},
};

static const cmd_lwip_name_map_t g_lwip_dir_map[] = {
    {"rx",   NETDBG_PACKET_DIR_RX},
    {"tx",   NETDBG_PACKET_DIR_TX},
    {"both", NETDBG_PACKET_DIR_BOTH},
    {"all",  NETDBG_PACKET_DIR_BOTH},
};

static void cmd_lwip_print_usage(void)
{
    os_printf(LM_CMD, LL_INFO,
              "lwip packet show\n"
              "lwip packet get\n"
              "lwip packet clear\n"
              "lwip packet set [ip <ipv4|u32>] [len <n>] [proto <tcp|udp|icmp|num>]\n"
              "                [sport <n>] [dport <n>] [port <n>] [dir <rx|tx|both|0|1|2>]\n"
              "                len 0: record only, no packet dump\n");
}

static const char *cmd_lwip_dir_to_str(unsigned char dir)
{
    if (dir == NETDBG_PACKET_DIR_RX) {
        return "rx";
    }

    if (dir == NETDBG_PACKET_DIR_TX) {
        return "tx";
    }

    if (dir == NETDBG_PACKET_DIR_BOTH) {
        return "both";
    }

    return "unknown";
}

static const char *cmd_lwip_proto_to_str(unsigned char proto)
{
    unsigned int i;

    for (i = 0; i < (sizeof(g_lwip_proto_map) / sizeof(g_lwip_proto_map[0])); i++) {
        if (g_lwip_proto_map[i].value == proto) {
            return g_lwip_proto_map[i].name;
        }
    }

    return "custom";
}

static void cmd_lwip_print_tcp_flags(const char *name, unsigned char flags)
{
    const char *sep = "";

    os_printf(LM_CMD, LL_INFO, "%s: 0x%02x", name, flags);
    if (flags == 0) {
        os_printf(LM_CMD, LL_INFO, " (none)\n");
        return;
    }

    os_printf(LM_CMD, LL_INFO, " (");
    if (flags & TCP_FIN) {
        os_printf(LM_CMD, LL_INFO, "%sFIN", sep);
        sep = "|";
    }
    if (flags & TCP_SYN) {
        os_printf(LM_CMD, LL_INFO, "%sSYN", sep);
        sep = "|";
    }
    if (flags & TCP_RST) {
        os_printf(LM_CMD, LL_INFO, "%sRST", sep);
        sep = "|";
    }
    if (flags & TCP_PSH) {
        os_printf(LM_CMD, LL_INFO, "%sPSH", sep);
        sep = "|";
    }
    if (flags & TCP_ACK) {
        os_printf(LM_CMD, LL_INFO, "%sACK", sep);
        sep = "|";
    }
    if (flags & TCP_URG) {
        os_printf(LM_CMD, LL_INFO, "%sURG", sep);
        sep = "|";
    }
    if (flags & TCP_ECE) {
        os_printf(LM_CMD, LL_INFO, "%sECE", sep);
        sep = "|";
    }
    if (flags & TCP_CWR) {
        os_printf(LM_CMD, LL_INFO, "%sCWR", sep);
    }
    os_printf(LM_CMD, LL_INFO, ")\n");
}

static int cmd_lwip_parse_u32(const char *arg, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed = 0;

    if (arg == NULL || value == NULL || arg[0] == '\0' || arg[0] == '-') {
        return -1;
    }

    parsed = strtoul(arg, &end, 0);
    if (end == arg || *end != '\0' || parsed > 0xFFFFFFFFUL) {
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

static int cmd_lwip_parse_u16(const char *arg, unsigned short *value)
{
    unsigned int parsed = 0;

    if (cmd_lwip_parse_u32(arg, &parsed) != 0 || parsed > 0xFFFFU) {
        return -1;
    }

    *value = (unsigned short)parsed;
    return 0;
}

static int cmd_lwip_parse_u8(const char *arg, unsigned char *value)
{
    unsigned int parsed = 0;

    if (cmd_lwip_parse_u32(arg, &parsed) != 0 || parsed > 0xFFU) {
        return -1;
    }

    *value = (unsigned char)parsed;
    return 0;
}

static int cmd_lwip_parse_ip(const char *arg, unsigned int *ipaddr)
{
    ip4_addr_t ip4addr;

    if (arg == NULL || ipaddr == NULL) {
        return -1;
    }

    if (ip4addr_aton(arg, &ip4addr)) {
        *ipaddr = ip4_addr_get_u32(&ip4addr);
        return 0;
    }

    return cmd_lwip_parse_u32(arg, ipaddr);
}

static int cmd_lwip_parse_proto(const char *arg, unsigned char *proto)
{
    unsigned int i;

    if (arg == NULL || proto == NULL) {
        return -1;
    }

    for (i = 0; i < (sizeof(g_lwip_proto_map) / sizeof(g_lwip_proto_map[0])); i++) {
        if (strcmp(arg, g_lwip_proto_map[i].name) == 0) {
            *proto = g_lwip_proto_map[i].value;
            return 0;
        }
    }

    return cmd_lwip_parse_u8(arg, proto);
}

static int cmd_lwip_parse_dir(const char *arg, unsigned char *dir)
{
    unsigned int i;
    unsigned char parsed = 0;

    if (arg == NULL || dir == NULL) {
        return -1;
    }

    for (i = 0; i < (sizeof(g_lwip_dir_map) / sizeof(g_lwip_dir_map[0])); i++) {
        if (strcmp(arg, g_lwip_dir_map[i].name) == 0) {
            *dir = g_lwip_dir_map[i].value;
            return 0;
        }
    }

    if (cmd_lwip_parse_u8(arg, &parsed) == 0 && parsed <= NETDBG_PACKET_DIR_BOTH) {
        *dir = parsed;
        return 0;
    }

    return -1;
}

static void cmd_lwip_print_mask(unsigned char mask)
{
    const char *sep = "";

    os_printf(LM_CMD, LL_INFO, "mask: 0x%02x", mask);
    if (mask == 0) {
        os_printf(LM_CMD, LL_INFO, " (none)\n");
        return;
    }

    os_printf(LM_CMD, LL_INFO, " (");
    if (mask & NETDBG_MASK_IPADDR) {
        os_printf(LM_CMD, LL_INFO, "%sip", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_LENGTH) {
        os_printf(LM_CMD, LL_INFO, "%slen", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_PROTCL) {
        os_printf(LM_CMD, LL_INFO, "%sproto", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_SRCPORT) {
        os_printf(LM_CMD, LL_INFO, "%ssport", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_DSTPORT) {
        os_printf(LM_CMD, LL_INFO, "%sdport", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_PORT) {
        os_printf(LM_CMD, LL_INFO, "%sport", sep);
        sep = "|";
    }
    if (mask & NETDBG_MASK_DIR) {
        os_printf(LM_CMD, LL_INFO, "%sdir", sep);
    }
    os_printf(LM_CMD, LL_INFO, ")\n");
}

static void cmd_lwip_print_rule(const netdbg_packet_t *rule)
{
    ip4_addr_t ip4addr;
    char ipbuf[IP4ADDR_STRLEN_MAX];

    if (rule == NULL) {
        return;
    }

    os_printf(LM_CMD, LL_INFO, "lwip packet rule:\n");
    cmd_lwip_print_mask(rule->mask);

    if (rule->mask & NETDBG_MASK_IPADDR) {
        ip4_addr_set_u32(&ip4addr, rule->ipaddr);
        ip4addr_ntoa_r(&ip4addr, ipbuf, sizeof(ipbuf));
        os_printf(LM_CMD, LL_INFO, "ip: %s (0x%08x)\n", ipbuf, rule->ipaddr);
    } else {
        os_printf(LM_CMD, LL_INFO, "ip: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_LENGTH) {
        if (rule->len == 0) {
            os_printf(LM_CMD, LL_INFO, "len: 0 (record only)\n");
        } else {
            os_printf(LM_CMD, LL_INFO, "len: %u\n", rule->len);
        }
    } else {
        os_printf(LM_CMD, LL_INFO, "len: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_PROTCL) {
        os_printf(LM_CMD, LL_INFO, "proto: %s(%u)\n",
                  cmd_lwip_proto_to_str(rule->packeType), rule->packeType);
    } else {
        os_printf(LM_CMD, LL_INFO, "proto: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_SRCPORT) {
        os_printf(LM_CMD, LL_INFO, "sport: %u\n", rule->srcPort);
    } else {
        os_printf(LM_CMD, LL_INFO, "sport: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_DSTPORT) {
        os_printf(LM_CMD, LL_INFO, "dport: %u\n", rule->dstPort);
    } else {
        os_printf(LM_CMD, LL_INFO, "dport: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_PORT) {
        os_printf(LM_CMD, LL_INFO, "port: %u\n", rule->port);
    } else {
        os_printf(LM_CMD, LL_INFO, "port: disabled\n");
    }

    if (rule->mask & NETDBG_MASK_DIR) {
        os_printf(LM_CMD, LL_INFO, "dir: %s(%u)\n",
                  cmd_lwip_dir_to_str(rule->dir), rule->dir);
    } else {
        os_printf(LM_CMD, LL_INFO, "dir: disabled\n");
    }

    cmd_lwip_print_tcp_flags("tcp rx flags", g_ipdbg.tcp.rx.flags);
    os_printf(LM_CMD, LL_INFO, "tcp rx seq max: %u\n", g_ipdbg.tcp.rx.max.seqNum);
    os_printf(LM_CMD, LL_INFO, "tcp rx ack max: %u\n", g_ipdbg.tcp.rx.max.ackNum);
    os_printf(LM_CMD, LL_INFO, "tcp last rx seq: %u\n", g_ipdbg.tcp.rx.last.seqNum);
    os_printf(LM_CMD, LL_INFO, "tcp last rx ack: %u\n", g_ipdbg.tcp.rx.last.ackNum);

    cmd_lwip_print_tcp_flags("tcp tx flags", g_ipdbg.tcp.tx.flags);
    os_printf(LM_CMD, LL_INFO, "tcp tx seq max: %u\n", g_ipdbg.tcp.tx.max.seqNum);
    os_printf(LM_CMD, LL_INFO, "tcp tx ack max: %u\n", g_ipdbg.tcp.tx.max.ackNum);
    os_printf(LM_CMD, LL_INFO, "tcp last tx seq: %u\n", g_ipdbg.tcp.tx.last.seqNum);
    os_printf(LM_CMD, LL_INFO, "tcp last tx ack: %u\n", g_ipdbg.tcp.tx.last.ackNum);
}

static int cmd_lwip_packet_show(void)
{
    netdbg_packet_t rule;

    if (netdbg_packet_rule_get(&rule) != 0) {
        os_printf(LM_CMD, LL_ERR, "failed to read packet rule\n");
        return CMD_RET_FAILURE;
    }

    cmd_lwip_print_rule(&rule);
    return CMD_RET_SUCCESS;
}

static int cmd_lwip_packet_clear(void)
{
    netdbg_packet_rule_clear();
    os_printf(LM_CMD, LL_INFO, "packet rule cleared\n");

    return cmd_lwip_packet_show();
}

static int cmd_lwip_packet_set(int argc, char *argv[])
{
    netdbg_packet_t rule;
    int index = 0;

    if (argc < 5 || ((argc - 3) & 0x1) != 0) {
        cmd_lwip_print_usage();
        return CMD_RET_USAGE;
    }

    memset(&rule, 0, sizeof(rule));

    /* set 子命令按 key/value 对解析，未出现的字段即不加入 mask。 */
    for (index = 3; index < argc; index += 2) {
        const char *key = argv[index];
        const char *value = argv[index + 1];

        if (strcmp(key, "ip") == 0) {
            if (cmd_lwip_parse_ip(value, &rule.ipaddr) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid ip: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_IPADDR;
        } else if (strcmp(key, "len") == 0) {
            if (cmd_lwip_parse_u32(value, &rule.len) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid len: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_LENGTH;
        } else if (strcmp(key, "proto") == 0 || strcmp(key, "type") == 0) {
            if (cmd_lwip_parse_proto(value, &rule.packeType) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid proto: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_PROTCL;
        } else if (strcmp(key, "sport") == 0 || strcmp(key, "srcport") == 0) {
            if (cmd_lwip_parse_u16(value, &rule.srcPort) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid sport: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_SRCPORT;
        } else if (strcmp(key, "dport") == 0 || strcmp(key, "dstport") == 0) {
            if (cmd_lwip_parse_u16(value, &rule.dstPort) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid dport: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_DSTPORT;
        } else if (strcmp(key, "port") == 0) {
            if (cmd_lwip_parse_u16(value, &rule.port) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid port: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_PORT;
        } else if (strcmp(key, "dir") == 0) {
            if (cmd_lwip_parse_dir(value, &rule.dir) != 0) {
                os_printf(LM_CMD, LL_ERR, "invalid dir: %s\n", value);
                return CMD_RET_FAILURE;
            }
            rule.mask |= NETDBG_MASK_DIR;
        } else {
            os_printf(LM_CMD, LL_ERR, "unknown key: %s\n", key);
            cmd_lwip_print_usage();
            return CMD_RET_USAGE;
        }
    }

    if (netdbg_packet_rule_set(&rule) != 0) {
        os_printf(LM_CMD, LL_ERR, "packet rule set failed\n");
        return CMD_RET_FAILURE;
    }

    os_printf(LM_CMD, LL_INFO, "packet rule updated\n");
    return cmd_lwip_packet_show();
}

static int cmd_lwip(int argc, char *argv[])
{

    if (argc < 2) {
        cmd_lwip_print_usage();
        return CMD_RET_USAGE;
    }

    if (strcmp(argv[1], "packet") != 0) {
        cmd_lwip_print_usage();
        return CMD_RET_USAGE;
    }

    if (argc < 3) {
        cmd_lwip_print_usage();
        return CMD_RET_USAGE;
    }

    if (strcmp(argv[2], "show") == 0 || strcmp(argv[2], "get") == 0) {
        if (argc != 3) {
            cmd_lwip_print_usage();
            return CMD_RET_USAGE;
        }
        return cmd_lwip_packet_show();
    }

    if (strcmp(argv[2], "clear") == 0) {
        if (argc != 3) {
            cmd_lwip_print_usage();
            return CMD_RET_USAGE;
        }
        return cmd_lwip_packet_clear();
    }

    if (strcmp(argv[2], "set") == 0) {
        return cmd_lwip_packet_set(argc, argv);
    }

    cmd_lwip_print_usage();
    return CMD_RET_USAGE;
}

CLI_CMD(lwip, cmd_lwip, "lwip debug command", "lwip packet <show|clear|set> ...");
