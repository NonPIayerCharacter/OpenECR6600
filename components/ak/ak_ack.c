#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "system_wifi.h"
#include "ak.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "lwip/tcpbase.h"
#include "lwip/netdb.h"

extern ak_result_t ak_result;
extern ak_local_context ak_context;
extern unsigned char cellphone_ip[4];

#define AK_ACK_TASK_PRIORITY             2   
#define AK_ACK_TASK_STACK_SIZE           2048

#define AK_ACK_TOUCH_DEVICE_PORT         7001
#define AK_ACK_TOUCH_SERVER_PORT         18266
#define AK_ACK_TOUCH_V2_SERVER_PORT(i)   (18266+i*10000)
#define AK_ACK_AIRKISS_SERVER_PORT       10000
#define AK_ACK_AIRKISS_DEVICE_PORT       10001
#define AK_ACK_AIRKISS_TIMEOUT           1500

#define AK_ACK_TOUCH_LEN                 11
#define AK_ACK_AIRKISS_LEN               7

#define AK_ACK_MAX_COUNT                 6000

#define AK_SEND_NUM  "AkSendNum"
#define AK_SEND_GAP  "AkSendGap"

static int ak_send_num = 30;
static int ak_send_gap = 100;

typedef enum {
    AK_TYPE_ESPTOUCH = 0,
    AK_TYPE_AIRKISS,
    AK_TYPE_ESPTOUCH_AIRKISS,
    AK_TYPE_ESPTOUCH_V2,
} ak_type_t;

typedef struct ak_ack {
    ak_type_t type;
    struct {
        uint8_t token;
        uint8_t mac[6];
        uint8_t ip[4];
    } ctx;
} ak_ack_t;

bool s_ak_ack_send = false;

static int ak_ack_send_get_errno(int fd)
{
    int sock_errno = 0;
    u32_t optlen = sizeof(sock_errno);

    getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);

    return sock_errno;
}

static void ak_ack_send_task(void* pvParameters)
{
	ak_ack_t* ack = (ak_ack_t*)pvParameters;
    struct ip_info local_ip;
    uint8_t remote_ip[4];
    memset(remote_ip, 0xFF, sizeof(remote_ip));
    struct sockaddr_in server_addr;
    socklen_t sin_size = sizeof(server_addr);
    int send_sock = -1;
    int optval = 1;
    int sendlen;
    int ack_len = AK_ACK_AIRKISS_LEN;
    int packet_count = 1;
    int err;
    int ret;
    int remote_port = 0;

    if (ack->type == AK_TYPE_ESPTOUCH) {
        remote_port = AK_ACK_TOUCH_SERVER_PORT;
    } else if (ack->type == AK_TYPE_ESPTOUCH_V2) {
        uint8_t port_bit =  ack->ctx.token;
        if(port_bit > 3) {
            port_bit = 0;
        }
        remote_port = AK_ACK_TOUCH_V2_SERVER_PORT(port_bit);
    } else {
        remote_port = AK_ACK_AIRKISS_SERVER_PORT;
    }

	memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, remote_ip, sizeof(remote_ip));
    server_addr.sin_port = htons(remote_port);

    wifi_get_mac_addr(STATION_IF, ack->ctx.mac);

    vTaskDelay(pdMS_TO_TICKS(200));
	
    while (s_ak_ack_send) {
        ret = wifi_get_ip_info(STATION_IF, &local_ip);		

        if ((true == ret) && !(ip_addr_isany_val(local_ip.ip))) {
            if (ack->type == AK_TYPE_ESPTOUCH) {
				#if LWIP_IPV4 && LWIP_IPV6
				memcpy(ack->ctx.ip, &local_ip.ip.u_addr.ip4.addr, 4);
				#elif LWIP_IPV4
				memcpy(ack->ctx.ip, &local_ip.ip.addr, 4);
				#endif
            }

            send_sock = socket(AF_INET, SOCK_DGRAM, 0);

            if ((send_sock < LWIP_SOCKET_OFFSET) || (send_sock > (FD_SETSIZE - 1))) {
                goto _end;
            }

            setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(int));
			setsockopt(send_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

            if (ack->type == AK_TYPE_AIRKISS) {
                char data = 0;
                struct sockaddr_in local_addr, from;
                socklen_t sockadd_len = sizeof(struct sockaddr);
				unsigned int akrecv_timeout = 1000;

                memset(&local_addr, 0, sizeof(struct sockaddr_in));
                memset(&from, 0, sizeof(struct sockaddr_in));
                local_addr.sin_family = AF_INET;
                local_addr.sin_addr.s_addr = INADDR_ANY;
                local_addr.sin_port = htons(AK_ACK_AIRKISS_DEVICE_PORT);

                bind(send_sock, (struct sockaddr*)&local_addr, sockadd_len);
				setsockopt(send_sock, SOL_SOCKET, SO_RCVTIMEO, &akrecv_timeout, sizeof(akrecv_timeout));

                recvfrom(send_sock, &data, 1, 0, (struct sockaddr*)&from, &sockadd_len);

                if (from.sin_addr.s_addr != INADDR_ANY) {
                    memcpy(remote_ip, &from.sin_addr, 4);
                    server_addr.sin_addr.s_addr = from.sin_addr.s_addr;
                } else {
                    
                }
			}

            while (s_ak_ack_send) {
                vTaskDelay(pdMS_TO_TICKS(ak_send_gap));
		
                sendlen = sendto(send_sock, &ack->ctx, ack_len, 0, (struct sockaddr*) &server_addr, sin_size);

                if (sendlen > 0) {
                    if (packet_count++ >= ak_send_num) {
                        goto _end;
                    }
                } else {
                    err = ak_ack_send_get_errno(send_sock);

                    if (err == ENOMEM || err == EAGAIN) {
                        continue;
                    }
                    goto _end;
                }
				
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

_end:

    if ((send_sock >= LWIP_SOCKET_OFFSET) && (send_sock <= (FD_SETSIZE - 1))) {
        close(send_sock);
    }
	
    os_free(ack);
    vTaskDelete(NULL);
}

int ak_send_ack_start()
{
    ak_ack_t* ack = NULL;

    ack = os_malloc(sizeof(ak_ack_t));

    if (ack == NULL) {
        return -1;
    }

	unsigned int tmp;
	char nv_str[8]={0};
	
	tmp = customer_get_env_blob(AK_SEND_NUM, nv_str, sizeof(nv_str)-1, NULL);
	if(tmp)
	{
		tmp = atoi(nv_str);
		if(tmp < 1)
			tmp = 1;
		ak_send_num = tmp;
	}
	
	tmp = customer_get_env_blob(AK_SEND_GAP, nv_str, sizeof(nv_str)-1, NULL);
	if(tmp)
	{
		tmp = atoi(nv_str);
		if(tmp < 1)
			tmp = 1;
		ak_send_gap = tmp;
	}


    ack->type = AK_TYPE_AIRKISS;
    ack->ctx.token = ak_result.random;
    memcpy(ack->ctx.ip, cellphone_ip, 4);

    s_ak_ack_send = true;

    if (xTaskCreate(ak_ack_send_task, "ak_ack_send_task", AK_ACK_TASK_STACK_SIZE, ack, AK_ACK_TASK_PRIORITY, NULL) != pdPASS) {
        os_free(ack);
        return -1;
    }

    return 0;
}

void ak_send_ack_stop(void)
{
    s_ak_ack_send = false;
}
