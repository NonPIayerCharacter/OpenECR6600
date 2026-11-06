#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "btc_blufi_prf.h"
#include "blufi_int.h"
#include "esp_log.h"
#include "esp_blufi_api.h"
#include "esp_err.h"
#include "btc/btc_task.h"
#include "esp_blufi.h"
#include "osi/allocator.h"
#include "bluetooth.h"
#if defined (CONFIG_NV)
#include "system_config.h"
#include "easyflash.h"
#endif
//#include "console/console.h"
#if 0
/*nimBLE Host*/
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif
#include "bluetooth.h"
#if (BLUFI_INCLUDED == TRUE)

#define BLUFI_SERVICE_MAX_NUM 1
#define BLUFI_CHAR_MAX_NUM 2

static uint16_t  conn_handle;
static ECR_BLE_GATTS_PARAMS_T		   ecr_blufi_gatt_service = {0};
static ECR_BLE_SERVICE_PARAMS_T 	   ecr_blufi_service[BLUFI_SERVICE_MAX_NUM] = {0};
static ECR_BLE_CHAR_PARAMS_T		   ecr_blufi_char[BLUFI_CHAR_MAX_NUM] = {0};

const static char *TAG = "BLUFI_EXAMPLE";
extern void ble_no_trans_send_data();

void ble_blufi_send_custom_data(uint8_t *data, uint32_t data_len)
{
    btc_msg_t msg;
    btc_blufi_args_t arg = {0};
    if(data == NULL || data_len == 0) {
        return;
    }
    ESP_BLE_HOST_STATUS_CHECK(ESP_BLE_HOST_STATUS_ENABLED);

    msg.sig = BTC_SIG_API_CALL;
    msg.pid = BTC_PID_BLUFI;
    msg.act = BTC_BLUFI_ACT_SEND_CUSTOM_DATA;
    arg.custom_data.data = data;
    arg.custom_data.data_len = data_len;

    btc_transfer_context(&msg, &arg, sizeof(btc_blufi_args_t), btc_blufi_call_deep_copy);
}

static void ecr_blufi_gatt_event(ECR_BLE_GATT_PARAMS_EVT_T *p_event)
{
	switch(p_event->type)
	{
		case ECR_BLE_GATT_EVT_WRITE_REQ:
			{
				if (p_event->gatt_event.write_report.char_handle == ecr_blufi_char[0].handle)
	        	{
					btc_blufi_recv_handler(&p_event->gatt_event.write_report.report.p_data[0], p_event->gatt_event.write_report.report.length);
				}
			}
			break;
		case ECR_BLE_GATT_EVT_NOTIFY_INDICATE_TX:
    		{
    		//    ESP_LOGI(TAG,"ECR_BLE_GATT_EVT_NOTIFY_INDICATE_TX\n");
    		    ble_no_trans_send_data();
    		}
			break;
    	case ECR_BLE_GATT_EVT_MTU_RSP:
    		{
		        ESP_LOGI(TAG, "mtu update event; conn_handle=%d mtu=%d\n",
		                 p_event->conn_handle,
		                 p_event->gatt_event.exchange_mtu);
		        blufi_env.frag_size = (p_event->gatt_event.exchange_mtu < BLUFI_MAX_DATA_LEN ? p_event->gatt_event.exchange_mtu : BLUFI_MAX_DATA_LEN) - BLUFI_MTU_RESERVED_SIZE;
        	}
			break;
		default:
			break;
	}
}

static void ecr_blufi_gap_event(ECR_BLE_GAP_PARAMS_EVT_T *p_event)
{
    switch (p_event->type) {
    case ECR_BLE_GAP_EVT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(TAG, "connection %s; status=%d\n",
                 p_event->result == 0 ? "established" : "failed",
                 p_event->result);
        if (p_event->result == 0) {

            blufi_env.is_connected = true;
            blufi_env.recv_seq = blufi_env.send_seq = 0;
            btc_msg_t msg;
            esp_blufi_cb_param_t param;
            msg.sig = BTC_SIG_API_CB;
            msg.pid = BTC_PID_BLUFI;
            msg.act = ESP_BLUFI_EVENT_BLE_CONNECT;
			
            memcpy(param.connect.remote_bda, p_event->gap_event.connect.peer_addr.addr, sizeof(esp_bd_addr_t));

            param.connect.conn_id = p_event->conn_handle;
            /* save connection handle */
            conn_handle = p_event->conn_handle;
            btc_transfer_context(&msg, &param, sizeof(esp_blufi_cb_param_t), NULL);
        }
        if (p_event->result != 0) {
            /* Connection failed; resume advertising. */
            esp_blufi_adv_start();
        }
        break;
    case ECR_BLE_GAP_EVT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d\n", p_event->gap_event.disconnect.reason);
        //memcpy(blufi_env.remote_bda, event->disconnect.conn.peer_id_addr.val, sizeof(esp_bd_addr_t));
        blufi_env.is_connected = false;
        blufi_env.recv_seq = blufi_env.send_seq = 0;
        blufi_env.sec_mode = 0x0;
        blufi_env.offset = 0;

        if (blufi_env.aggr_buf != NULL) {
            osi_free(blufi_env.aggr_buf);
            blufi_env.aggr_buf = NULL;
        }

        btc_msg_t msg;
        esp_blufi_cb_param_t param;
		memset(&param, 0, sizeof(esp_blufi_cb_param_t));

        msg.sig = BTC_SIG_API_CB;
        msg.pid = BTC_PID_BLUFI;
        msg.act = ESP_BLUFI_EVENT_BLE_DISCONNECT;
        //memcpy(param.disconnect.remote_bda, event->disconnect.conn.peer_id_addr.val, sizeof(esp_bd_addr_t));
        btc_transfer_context(&msg, &param, sizeof(esp_blufi_cb_param_t), NULL);

        break;
	default:
		break;
    }
}

void ecr_blufi_gatt_svr_init(void)
{
	ecr_ble_gap_callback_register(ecr_blufi_gap_event);
	ecr_ble_gatt_callback_register(ecr_blufi_gatt_event);
	
	ECR_BLE_GATTS_PARAMS_T *p_ble_service = &ecr_blufi_gatt_service;
	p_ble_service->svc_num =  BLUFI_SERVICE_MAX_NUM;
	p_ble_service->p_service = ecr_blufi_service;

	ECR_BLE_SERVICE_PARAMS_T *p_common_service = ecr_blufi_service;
	p_common_service->handle = ECR_BLE_GATT_INVALID_HANDLE;
	p_common_service->svc_uuid.uuid_type   =  ECR_BLE_UUID_TYPE_16;
	p_common_service->svc_uuid.uuid.uuid16 =  BLUFI_APP_UUID;
	p_common_service->type	   = ECR_BLE_UUID_SERVICE_PRIMARY;
	p_common_service->char_num = BLUFI_CHAR_MAX_NUM;
	p_common_service->p_char   = ecr_blufi_char;
	
	/*Add write characteristic*/
	ECR_BLE_CHAR_PARAMS_T *p_common_char = ecr_blufi_char;
	p_common_char->handle = ECR_BLE_GATT_INVALID_HANDLE;
	p_common_char->char_uuid.uuid_type	 = ECR_BLE_UUID_TYPE_16;
	p_common_char->char_uuid.uuid.uuid16 = BLUFI_CHAR_P2E_UUID;
	
	p_common_char->property = ECR_BLE_GATT_CHAR_PROP_WRITE;
	p_common_char->permission = ECR_BLE_GATT_PERM_READ | ECR_BLE_GATT_PERM_WRITE;
	p_common_char->value_len = BLE_ATT_MTU_DEF;
	p_common_char->cccd = UUID_CLIENT_CHAR_CFG;
	p_common_char++;

	/*Add READ/Notify characteristic*/
	p_common_char->handle = ECR_BLE_GATT_INVALID_HANDLE;
	p_common_char->char_uuid.uuid_type	 = ECR_BLE_UUID_TYPE_16;
	p_common_char->char_uuid.uuid.uuid16 = BLUFI_CHAR_E2P_UUID;
	
	p_common_char->property = ECR_BLE_GATT_CHAR_PROP_NOTIFY | ECR_BLE_GATT_CHAR_PROP_READ;
	p_common_char->permission = ECR_BLE_GATT_PERM_READ | ECR_BLE_GATT_PERM_WRITE;
	p_common_char->value_len = BLE_ATT_MTU_DEF;
	p_common_char->cccd = UUID_CLIENT_CHAR_CFG;

	ecr_ble_reset();
	ecr_ble_gatts_service_add(p_ble_service, false);
}

void esp_blufi_adv_start(void)
{
    int rc;

	char dev_name[27]={0};
	#if defined (CONFIG_NV)
    hal_system_get_config(CUSTOMER_NV_BLUFI_NAME, (void *)dev_name, sizeof(dev_name));
    if(0 == strlen(dev_name))
    {
        ESP_LOGE(TAG, "blufi get name failed\n");
        memcpy(dev_name, BLUFI_DEVICE_NAME, strlen(BLUFI_DEVICE_NAME));
        if(EF_NO_ERR != hal_system_set_config(CUSTOMER_NV_BLUFI_NAME, dev_name, strlen(BLUFI_DEVICE_NAME)))
        {
            ESP_LOGE(TAG, "blufi restore name failed\n");
        }
    }
    #else 
    memcpy(dev_name, BLUFI_DEVICE_NAME, strlen(BLUFI_DEVICE_NAME));
    #endif //CONFIG_NV

	uint8_t index = 0;
	uint8_t advData[31];
	ECR_BLE_DATA_T p_adv_data;
	ECR_BLE_DATA_T p_scan_rsp_data;

	memset(&p_adv_data, 0, sizeof(ECR_BLE_DATA_T));
	memset(&p_scan_rsp_data, 0, sizeof(ECR_BLE_DATA_T));
	
	advData[index++] = 0x02;											// length
	advData[index++] = 0x01; 										    // AD type : flags
	advData[index++] = 0x06;											// AD value
	
	advData[index++] = strlen(dev_name)+1;				   				// length
	advData[index++] = 0x09;									   		// AD type :device name
	memcpy(&advData[index], dev_name, strlen(dev_name));				// AD value
	index += strlen(dev_name);

    if(index < 29 )
    {
        advData[index++] = 0x02; 	   										// length
        advData[index++] = 0x0a; 											// AD type :txpower
        advData[index++] = 0x06; 											// AD value
    }
	
    if(index < 28)
    {
        advData[index++] = 0x03; 	   										// length
        advData[index++] = 0x03; 											// AD type :16bit service uuid
        advData[index++] = (BLUFI_APP_UUID & 0xff); 						// AD value
        advData[index++] = ((BLUFI_APP_UUID>>8)&0xff); 						// AD value
    }
	
	p_adv_data.length = index;
	p_adv_data.p_data = advData;
	ecr_ble_adv_param_set(&p_adv_data, &p_scan_rsp_data);
	
	ECR_BLE_GAP_ADV_PARAMS_T adv_param={0};
	memset(&adv_param, 0, sizeof(ECR_BLE_GAP_ADV_PARAMS_T));
	adv_param.adv_type=ECR_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED;

	adv_param.adv_interval_max=64;
	adv_param.adv_interval_min=64;
	adv_param.adv_channel_map=0x07;
	
    rc = ecr_ble_gap_adv_start(&adv_param);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

uint8_t esp_blufi_init(void)
{
    blufi_env.enabled = true;
    esp_blufi_cb_param_t param;
    param.init_finish.state = ESP_BLUFI_INIT_OK;
    btc_blufi_cb_to_app(ESP_BLUFI_EVENT_INIT_FINISH, &param);
    return ESP_BLUFI_ERROR;
}

void esp_blufi_deinit(void)
{
    blufi_env.enabled = false;
    btc_msg_t msg;
    esp_blufi_cb_param_t param;
	msg.sig = BTC_SIG_API_CB;
    msg.pid = BTC_PID_BLUFI;
    msg.act = ESP_BLUFI_EVENT_DEINIT_FINISH;
    param.deinit_finish.state = ESP_BLUFI_DEINIT_OK;
    btc_transfer_context(&msg, &param, sizeof(esp_blufi_cb_param_t), NULL);
}

void esp_blufi_send_notify(void *arg)
{
    struct pkt_info *pkts = (struct pkt_info *) arg;
	#if 0
    struct os_mbuf *om;
    om = ble_hs_mbuf_from_flat(pkts->pkt, pkts->pkt_len);
    int rc = 0;
    rc = ble_gattc_notify_custom(conn_handle, gatt_values[1].val_handle, om);
    assert(rc == 0);
	#endif
	ecr_ble_gatts_value_notify(conn_handle, ecr_blufi_char[1].handle, pkts->pkt, pkts->pkt_len);
}

uint16_t esp_blufi_get_notify_hanlder(void)
{
    return ecr_blufi_char[1].handle;
}

uint16_t esp_blufi_get_conn_hanlder(void)
{
    return conn_handle;
}

void esp_blufi_disconnect(void)
{
	ecr_ble_gap_disconnect(conn_handle, BT_ERROR_REMOTE_USER_TERM_CON);
}

void esp_blufi_adv_stop(void) {}

void esp_blufi_send_encap(void *arg)
{
    struct blufi_hdr *hdr = (struct blufi_hdr *)arg;
    if (blufi_env.is_connected == false) {
        BTC_TRACE_WARNING("%s ble connection is broken\n", __func__);
        osi_free(hdr);
        hdr =  NULL;
        return;
    }
    btc_blufi_send_notify((uint8_t *)hdr,
                          ((hdr->fc & BLUFI_FC_CHECK) ?
                           hdr->data_len + sizeof(struct blufi_hdr) + 2 :
                           hdr->data_len + sizeof(struct blufi_hdr)));
}


void ecr_blufi_btc_init(void)
{
    int rc;
    rc = btc_init();
    assert(rc == 0);
}

void ecr_blufi_btc_deinit(void)
{
    btc_deinit();
}

void *ecr_blufi_get_queue()
{
    return btc_get_queue();
}

#endif
