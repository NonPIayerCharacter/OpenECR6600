
extern uint32_t socket_coninfo;

#define TAG_LOG "[example]"

#define TCP_FUNC_SET_START(x) ((x) |= BIT0)
#define TCP_FUNC_ISSET_START(x) (((x) & BIT0) != 0)
#define TCP_FUNC_UNSET_START(x) ((x) &= ~BIT0)
#define TCPV6_FUNC_SET_START(x) ((x) |= BIT1)
#define TCPV6_FUNC_ISSET_START(x) (((x) & BIT1) != 0)
#define TCPV6_FUNC_UNSET_START(x) ((x) &= ~BIT1)
#define UDP_FUNC_SET_START(x) ((x) |= BIT2)
#define UDP_FUNC_ISSET_START(x) (((x) & BIT2) != 0)
#define UDP_FUNC_UNSET_START(x) ((x) &= ~BIT2)
#define UDPV6_FUNC_SET_START(x) ((x) |= BIT3)
#define UDPV6_FUNC_ISSET_START(x) (((x) & BIT3) != 0)
#define UDPV6_FUNC_UNSET_START(x) ((x) &= ~BIT3)

#define TCP_SERVER_FUNC_SET_START(x) ((x) |= BIT4)
#define TCP_SERVER_FUNC_ISSET_START(x) (((x) & BIT4) != 0)
#define TCP_SERVER_FUNC_UNSET_START(x) ((x) &= ~BIT4)

#define TCPV6_SERVER_FUNC_SET_START(x) ((x) |= BIT5)
#define TCPV6_SERVER_FUNC_ISSET_START(x) (((x) & BIT5) != 0)
#define TCPV6_SERVER_FUNC_UNSET_START(x) ((x) &= ~BIT5)

#define UDP_SERVER_FUNC_SET_START(x) ((x) |= BIT6)
#define UDP_SERVER_FUNC_ISSET_START(x) (((x) & BIT6) != 0)
#define UDP_SERVER_FUNC_UNSET_START(x) ((x) &= ~BIT6)

#define UDPV6_SERVER_FUNC_SET_START(x) ((x) |= BIT7)
#define UDPV6_SERVER_FUNC_ISSET_START(x) (((x) & BIT7) != 0)
#define UDPV6_SERVER_FUNC_UNSET_START(x) ((x) &= ~BIT7)