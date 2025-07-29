/**
 ****************************************************************************************
 *
 * @file ble_spi_cmd.c
 *
 * @brief BLE SPI Command Source Code
 *
 * @par Copyright (C):
 *      ESWIN 2015-2020
 *
 ****************************************************************************************
 */

/****************************************************************************************
 * 
 * INCLUDE FILES
 *
 ****************************************************************************************/
#include <string.h>
#include "ble_spi_cmd.h"
#include "bluetooth.h"
#include "spi_service_main.h"
#include "oshal.h"
#if defined (CONFIG_NV)
#include "system_config.h"
#endif

#define BLE_DEVICE_NAME_MAX_LEN              (18)
#define BLE_SPI_GATT_SERVICE_MAX_NUM         (1)
#define BLE_SPI_GATT_CHAR_MAX_NUM            (3)
#define BLE_SPI_INVALID_CONIDX               (0xff)
#define BLE_SPI_SERVICE_UUID                 (0x180b)
#define BLE_SPI_WRITE_CHAR_UUID              (0x2a2d)
#define BLE_SPI_READ_CHAR_UUID               (0x2a2e)
#define BLE_SPI_NOTIFY_CHAR_UUID             (0x2a2f)

#define BLE_COMMAND_CHECK_RETURN(condition, ret, logstr)\
do { \
    if ((condition) != 0) { \
        if ((logstr) != NULL) { \
            os_printf(LM_BLE, LL_ERR, "%s %d %s, ret: %d\r\n", __func__, __LINE__, logstr, ret); \
        } \
        return ret; \
    } \
} while (0)

typedef struct {
    int task_handle;
    os_queue_handle_t msg_queue;
    uint16_t conn_handler;
    uint16_t ntf_ind_handler;
} ble_spi_priv_t;

static ble_spi_priv_t g_ble_priv;
static ECR_BLE_GATTS_PARAMS_T          ble_spi_gatt_service = {0};
static ECR_BLE_SERVICE_PARAMS_T        ble_spi_common_service[BLE_SPI_GATT_SERVICE_MAX_NUM] = {0};
static ECR_BLE_CHAR_PARAMS_T           ble_spi_common_char[BLE_SPI_GATT_CHAR_MAX_NUM] = {0};

static ble_spi_priv_t *ble_spi_get_priv(void)
{
    return &g_ble_priv;
}

static void ble_spi_set_adv_data(ECR_BLE_DATA_T *pdata)
{
    uint16_t index = 0;
    static uint8_t adv_data[31] = {0};
    char adv_mode[3] = {2,1,6};
    char service_uuid[4] = {3,2,1,0xa2};

    //set adv mode --- flag
    memcpy(&adv_data[index], adv_mode, sizeof(adv_mode)/sizeof(adv_mode[0]));
    index += sizeof(adv_mode)/sizeof(adv_mode[0]);

    //set service uuid
    memcpy(&adv_data[index], service_uuid, sizeof(service_uuid)/sizeof(service_uuid[0]));
    index += sizeof(service_uuid)/sizeof(service_uuid[0]);

    pdata->length = index;
    pdata->p_data = adv_data;
}

static void ble_spi_set_scan_rsp_data(ECR_BLE_DATA_T *pdata)
{
    uint16_t index = 0;
    static uint8_t rsp_data[31] = {0};

    char temp_name[BLE_DEVICE_NAME_MAX_LEN] = {0};
    char default_name[10] = {0x09,0x09,'E','S','W','-','6','6','0','0'};

    memcpy(&rsp_data[index], temp_name, strlen(temp_name));
    int ret = hal_system_get_config(CUSTOMER_NV_BLE_DEVICE_NAME, temp_name, sizeof(temp_name));
    if (ret)
    {
        //set nv device name
        rsp_data[index++] = strlen(temp_name)+1;
        rsp_data[index++] = 0x09;
        memcpy(&rsp_data[index], temp_name, strlen(temp_name));
        index += strlen(temp_name);
    }
    else
    {
        //set default device name
        memcpy(&rsp_data[index], default_name, sizeof(default_name)/sizeof(default_name[0]));
        index += sizeof(default_name)/sizeof(default_name[0]);
    }
    pdata->length = index;
    pdata->p_data = rsp_data;
}

static void ble_spi_task(void *arg)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ble_spi_msg_t msg = {0};
    int ret;

    do {
        ret = os_queue_receive(priv->msg_queue, (char *)&msg, sizeof(ble_spi_msg_t), WAIT_FOREVER);
        if (ret < 0) {
            os_printf(LM_OS, LL_ERR, "ble spi queue recv error.\n");
            break;
        }

        ble_spi_msg_recv_handler((uint8_t *)msg.msgAddr, msg.msgLen);

        if (msg.msgfree != NULL) {
            msg.msgfree((void *)msg.msgAddr);
        }
    } while(1);
}

static void ble_spi_send_event(uint8_t type, uint8_t *data, int data_len)
{
    struct ble_spi_hdr *hdr = os_malloc(sizeof(struct ble_spi_hdr) + data_len);
    if (hdr == NULL) {
        return;
    }
    hdr->type = type;
    hdr->data_len = data_len;
    memcpy(hdr->data, data, data_len);

    spi_service_send_msg((unsigned char *)hdr, sizeof(struct ble_spi_hdr) + data_len);

    if (hdr != NULL) {
        os_free(hdr);
        hdr = NULL;
    }
}

static void ble_spi_gap_event_cb(ECR_BLE_GAP_PARAMS_EVT_T *p_event)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    uint8_t type;

    switch(p_event->type)
    {
        case ECR_BLE_GAP_EVT_CONNECT:
        {
            if(BT_ERROR_NO_ERROR == p_event->result) {
                os_printf(LM_CMD, LL_INFO, "Device connect succcess\n");
                priv->conn_handler = p_event->conn_handle;
            } else {
                os_printf(LM_CMD, LL_INFO, "Device connect fail\n");
            }
            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_CONNECT);
            ble_spi_send_event(type, (uint8_t *)&p_event->conn_handle, 2);
        }
        break;
        case ECR_BLE_GAP_EVT_DISCONNECT:
        {
            priv->conn_handler = 0;
            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_DISCONNECT);
            ble_spi_send_event(type, (uint8_t *)&p_event->conn_handle, 2);
        }
        break;
        case ECR_BLE_GAP_EVT_ADV_REPORT:
        {
            if((p_event->gap_event.adv_report.data.p_data == NULL)
                || (p_event->gap_event.adv_report.data.length == 0)) {
                //empty packet
                return;
            }
            os_printf(LM_CMD, LL_INFO, "adv_addr");
            for (int8_t i=5;i>=0;i--) {
                os_printf(LM_CMD, LL_INFO, ":%02X",p_event->gap_event.adv_report.peer_addr.addr[i]);
            }
            os_printf(LM_CMD, LL_INFO, "\n\r");
            os_printf(LM_CMD, LL_INFO, "rssi:%d dBm\n\r",p_event->gap_event.adv_report.rssi);

            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_ADV_REPORT);
            ble_spi_send_event(type, p_event->gap_event.adv_report.peer_addr.addr, 6);
        }
        break;
        default:
            break;
    }
}

static void ble_spi_gatt_event_cb(ECR_BLE_GATT_PARAMS_EVT_T *p_event)
{
    uint8_t type;

    switch(p_event->type)
    {
        case ECR_BLE_GATT_EVT_MTU_REQUEST:
        {
            if (p_event->result != BT_ERROR_NO_ERROR) {
                type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_MTUREQ);
                ble_spi_send_event(type, (uint8_t *)&p_event->result, 1);
            }
        }
        break;
        case ECR_BLE_GATT_EVT_MTU_RSP:
        {
            if (p_event->result == BT_ERROR_NO_ERROR) {
                type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_DATA, BLE_SPI_TYPE_DATA_SUBTYPE_RECV_MTU_RSP);
                ble_spi_send_event(type, (uint8_t *)&p_event->gatt_event.exchange_mtu, 2);
            }
        }
        break;
        case ECR_BLE_GATT_EVT_WRITE_REQ:
        {
            char *write_data = (char *)p_event->gatt_event.write_report.report.p_data;
            uint8_t len = p_event->gatt_event.write_report.report.length;
            write_data[len] = '\0';
            os_printf(LM_CMD, LL_INFO, "Char handle:%#02x, write_data:%s\n", p_event->gatt_event.write_report.char_handle,write_data);

            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_DATA, BLE_SPI_TYPE_DATA_SUBTYPE_RECV_WRITE);
            ble_spi_send_event(type, p_event->gatt_event.write_report.report.p_data, p_event->gatt_event.write_report.report.length);
        }
        break;

        case ECR_BLE_GATT_EVT_WRITE_CMP:
        {
            if(p_event->gatt_event.write_result.result == BT_ERROR_NO_ERROR)
            {
                os_printf(LM_BLE,LL_INFO,"The operation of write char handle %#02x is proceed\n",p_event->gatt_event.write_result.char_handle);
            }
            else
            {
                os_printf(LM_BLE,LL_INFO,"The operation of write char handle %#02x is failed,reason:%#02x\n",p_event->gatt_event.write_result.char_handle,
                                                                                            p_event->gatt_event.write_result.result);
            }

            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_SEND_WRITE);
            ble_spi_send_event(type, (uint8_t *)&p_event->gatt_event.write_result.result, 1);
        }
        break;

        case ECR_BLE_GATT_EVT_READ_RX:
        {
            os_printf(LM_BLE,LL_INFO,"read_data: ");
            for(uint8_t i = 0; i < p_event->gatt_event.data_read.report.length; i++)
                os_printf(LM_BLE,LL_INFO,"%x ",p_event->gatt_event.data_read.report.p_data[i]);
            os_printf(LM_BLE,LL_INFO,"\n");

            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_DATA, BLE_SPI_TYPE_DATA_SUBTYPE_RECV_READ_RSP);
            ble_spi_send_event(type, p_event->gatt_event.data_read.report.p_data, p_event->gatt_event.data_read.report.length);
        }
        break;

        case ECR_BLE_GATT_EVT_NOTIFY_INDICATE_RX:
        {
            os_printf(LM_CMD, LL_INFO, "notify/indicate char handle=%#02x\n",p_event->gatt_event.data_report.char_handle);
            os_printf(LM_CMD, LL_INFO, "notify/indicate char value length=%#02x\n",p_event->gatt_event.data_report.report.length);
            os_printf(LM_CMD, LL_INFO, "notify/indicate char value=");
            for(int i=0;i<p_event->gatt_event.data_report.report.length;i++)
            {
                os_printf(LM_BLE,LL_INFO,"%c",p_event->gatt_event.data_report.report.p_data[i]);
            }
            os_printf(LM_BLE,LL_INFO,"\n");

            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_DATA, BLE_SPI_TYPE_DATA_SUBTYPE_RECV_NOTIFY);
            ble_spi_send_event(type, p_event->gatt_event.data_report.report.p_data, p_event->gatt_event.data_report.report.length);
        }
        break;

        case ECR_BLE_GATT_EVT_NOTIFY_INDICATE_TX:
        {
            if (p_event->gatt_event.notify_result.result == BT_ERROR_NO_ERROR) {
                os_printf(LM_BLE,LL_INFO,"The operation of notify/indicate char handle %#02x is proceed\n",p_event->gatt_event.notify_result.char_handle);
            } else {
                os_printf(LM_BLE,LL_INFO,"The operation of notify/indicate char handle %#02x is failed,reason:%#02x\n",p_event->gatt_event.notify_result.char_handle,
                                                                                            p_event->gatt_event.notify_result.result);
            }
            type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_SEND_NOTIFY);
            ble_spi_send_event(type, (uint8_t *)&p_event->gatt_event.notify_result.result, 1);
        }
        break;

        case ECR_BLE_GATT_EVT_CHAR_DISCOVERY:
        {
            if(p_event->result != BT_ERROR_DISC_DONE)
            {
                os_printf(LM_CMD, LL_INFO, "char handle=%#02x\n",p_event->gatt_event.char_disc.handle);
                if(ECR_BLE_UUID_TYPE_16 == p_event->gatt_event.char_disc.uuid.uuid_type)
                    os_printf(LM_CMD, LL_INFO, "char uuid=%#04x\n",p_event->gatt_event.char_disc.uuid.uuid.uuid16);
                else if(ECR_BLE_UUID_TYPE_32 == p_event->gatt_event.char_disc.uuid.uuid_type)
                    os_printf(LM_CMD, LL_INFO, "char uuid=%#x\n",p_event->gatt_event.char_disc.uuid.uuid.uuid32);
                else if(ECR_BLE_UUID_TYPE_128 == p_event->gatt_event.char_disc.uuid.uuid_type)
                    os_printf(LM_CMD, LL_INFO, "char uuid=%#x\n",p_event->gatt_event.char_disc.uuid.uuid.uuid128);
            }
        }
        break;
        default:
            break;
	}
}

void ble_spi_msg_recv_handler(uint8_t *data, int len)
{
    struct ble_spi_hdr *hdr = (struct ble_spi_hdr *)data;

    if (BLE_SPI_TYPE_IS_CTRL(hdr->type)) {
        switch (BLE_SPI_SUBTYPE(hdr->type)) {
        case BLE_SPI_TYPE_CTRL_SUBTYPE_INIT:
            ble_spi_role_init(hdr->data[0]);
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_DEINIT:
            ble_spi_role_deinit();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_START_ADV:
            ble_spi_adv_start();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_STOP_ADV:
            ble_spi_adv_stop();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_START_SCAN:
            ble_spi_start_scan();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_STOP_SCAN:
            ble_spi_stop_scan();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_CONNECT:
            ble_spi_start_connect(hdr->data);
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_DISCONNECT:
            ble_spi_start_disconnect();
            break;
        case BLE_SPI_TYPE_CTRL_SUBTYPE_GETADDR:
            ble_spi_get_self_addr();
            break;
        default:
            os_printf(LM_BLE, LL_INFO, "BLE Spi Recv Ctrl Subtype Error\n");
            break;
        }
    } else if (BLE_SPI_TYPE_IS_DATA(hdr->type)) {
        switch (BLE_SPI_SUBTYPE(hdr->type)) {
        case BLE_SPI_TYPE_DATA_SUBTYPE_SEND_MTU:
        {
            uint16_t mtu_val = hdr->data[0] | hdr->data[1] << 8;
            ble_spi_gattc_exchange_mtu_req(mtu_val);
            break;
        }
        case BLE_SPI_TYPE_DATA_SUBTYPE_SEND_WRITE:
        {
            uint16_t char_handle = hdr->data[0] | hdr->data[1] << 8;
            uint16_t data_len = hdr->data_len - 2;
            ble_spi_gattc_write(char_handle, data_len, &hdr->data[2]);
            break;
        }
        case BLE_SPI_TYPE_DATA_SUBTYPE_SEND_READ:
        {
            uint16_t char_handle = hdr->data[0] | hdr->data[1] << 8;
            ble_spi_gattc_read(char_handle);
            break;
        }
        case BLE_SPI_TYPE_DATA_SUBTYPE_SEND_NOTIFY:
            ble_spi_gatts_notify(hdr->data, hdr->data_len);
            break;
        default:
            os_printf(LM_BLE, LL_INFO, "BLE Spi Recv Data Subtype Error\n");
            break;
        }
    }
}

int ble_spi_role_init(uint8_t role)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    BLE_COMMAND_CHECK_RETURN(role > BLE_ROLE_SLAVE, -1, "Ipnut dev role error");

    ecr_ble_reset();
    ecr_ble_gap_callback_register(ble_spi_gap_event_cb);
    ecr_ble_gatt_callback_register(ble_spi_gatt_event_cb);

    memset(ble_spi_common_service, 0x00, sizeof(ECR_BLE_SERVICE_PARAMS_T)*BLE_SPI_GATT_SERVICE_MAX_NUM);

    if (role == BLE_ROLE_SLAVE) {
        ECR_BLE_GATTS_PARAMS_T *p_ble_service = &ble_spi_gatt_service;
        p_ble_service->svc_num =  BLE_SPI_GATT_SERVICE_MAX_NUM;
        p_ble_service->p_service = ble_spi_common_service;

        /*First service add*/
        ECR_BLE_SERVICE_PARAMS_T *p_common_service = ble_spi_common_service;
        p_common_service->handle = ECR_BLE_GATT_INVALID_HANDLE;
        p_common_service->svc_uuid.uuid_type   =  ECR_BLE_UUID_TYPE_16;
        p_common_service->svc_uuid.uuid.uuid16 =  BLE_SPI_SERVICE_UUID;
        p_common_service->type     = ECR_BLE_UUID_SERVICE_PRIMARY;
        p_common_service->char_num = BLE_SPI_GATT_CHAR_MAX_NUM;
        p_common_service->p_char   = ble_spi_common_char;

        /*Add write characteristic*/
        ECR_BLE_CHAR_PARAMS_T *p_common_char = ble_spi_common_char;
        p_common_char->handle = ECR_BLE_GATT_INVALID_HANDLE;
        p_common_char->char_uuid.uuid_type   = ECR_BLE_UUID_TYPE_16;
        p_common_char->char_uuid.uuid.uuid16 = BLE_SPI_WRITE_CHAR_UUID;
        p_common_char->property = ECR_BLE_GATT_CHAR_PROP_WRITE_NO_RSP;
        p_common_char->permission = ECR_BLE_GATT_PERM_READ | ECR_BLE_GATT_PERM_WRITE;
        p_common_char->value_len = 252;
        p_common_char++;

        /*Add Notify characteristic*/
        p_common_char->handle = ECR_BLE_GATT_INVALID_HANDLE;
        p_common_char->char_uuid.uuid_type   = ECR_BLE_UUID_TYPE_16;
        p_common_char->char_uuid.uuid.uuid16 = BLE_SPI_NOTIFY_CHAR_UUID;
        p_common_char->property = ECR_BLE_GATT_CHAR_PROP_NOTIFY | ECR_BLE_GATT_CHAR_PROP_INDICATE;
        p_common_char->permission = ECR_BLE_GATT_PERM_READ | ECR_BLE_GATT_PERM_WRITE;
        p_common_char->value_len = 252;
        p_common_char++;

        /*Add Read && write characteristic*/
        p_common_char->handle = ECR_BLE_GATT_INVALID_HANDLE;
        p_common_char->char_uuid.uuid_type   = ECR_BLE_UUID_TYPE_16;
        p_common_char->char_uuid.uuid.uuid16 = BLE_SPI_READ_CHAR_UUID;

        p_common_char->property = ECR_BLE_GATT_CHAR_PROP_READ | ECR_BLE_GATT_CHAR_PROP_WRITE;
        p_common_char->permission = ECR_BLE_GATT_PERM_READ | ECR_BLE_GATT_PERM_WRITE;
        p_common_char->value_len = 252;

        ecr_ble_gatts_service_add(p_ble_service, false);

        if (ble_spi_common_char[1].handle != ECR_BLE_GATT_INVALID_HANDLE) {
            priv->ntf_ind_handler = ble_spi_common_char[1].handle;
        }
    }
    return BT_ERROR_NO_ERROR;
}

void ble_spi_role_deinit()
{
    ecr_ble_reset();
}

int ble_spi_adv_start(void)
{
    int ret;
    ECR_BLE_DATA_T  padv_data = {0};
    ECR_BLE_DATA_T  prsp_data = {0};

    ECR_BLE_GAP_ADV_PARAMS_T adv_param = {0};
    adv_param.adv_type = ECR_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED;
    adv_param.adv_interval_min = 0x20;
    adv_param.adv_interval_max = 0x20;
    adv_param.adv_channel_map  = 0x07;

    ble_spi_set_adv_data(&padv_data);
    ble_spi_set_scan_rsp_data(&prsp_data);
    ecr_ble_adv_param_set(&padv_data, &prsp_data);

    ret = ecr_ble_gap_adv_start(&adv_param);
    BLE_COMMAND_CHECK_RETURN(ret != BT_ERROR_NO_ERROR, ret, "start adv fail");

    return BT_ERROR_NO_ERROR;
}

int ble_spi_adv_stop(void)
{
    int ret = ecr_ble_gap_adv_stop();
    BLE_COMMAND_CHECK_RETURN(ret != BT_ERROR_NO_ERROR, ret, "stop adv fail");

    return BT_ERROR_NO_ERROR;
}

int ble_spi_start_scan(void)
{
    int ret = BT_ERROR_NO_ERROR;
    ECR_BLE_GAP_SCAN_PARAMS_T scan_param ={0};

    scan_param.scan_type= ECR_SCAN_TYPE_GEN_DISC;
    scan_param.dup_filt_pol = ECR_DUP_FILT_EN;
    scan_param.scan_prop= ECR_BLE_SCAN_PROP_PHY_1M_BIT | ECR_BLE_SCAN_PROP_ACTIVE_1M_BIT;

    scan_param.interval = 0x20;
    scan_param.window = 0x20;
    scan_param.duration = 1000;
    scan_param.period = 0;
    scan_param.scan_channel_map = 0x07;

    ret = ecr_ble_gap_scan_start(&scan_param);
    BLE_COMMAND_CHECK_RETURN(ret != BT_ERROR_NO_ERROR, ret, "start scan fail");

    return BT_ERROR_NO_ERROR;
}

int ble_spi_stop_scan(void)
{
    int ret = ecr_ble_gap_scan_stop();
    BLE_COMMAND_CHECK_RETURN(ret != BT_ERROR_NO_ERROR, ret, "stop scan fail");

    return BT_ERROR_NO_ERROR;
}

int ble_spi_start_connect(uint8_t *peer_addr)
{
    ECR_BLE_GAP_CONN_PARAMS_T conn_param;
    ECR_BLE_GAP_ADDR_T p_peer_addr;
    int ret;

    memset(&p_peer_addr, 0x00, sizeof(p_peer_addr));
    memset(&conn_param, 0x00, sizeof(conn_param));

    conn_param.conn_interval_min = 12;
    conn_param.conn_interval_max = 12;
    conn_param.conn_latency = 0;
    conn_param.conn_sup_timeout = 500;
    conn_param.conn_establish_timeout = 1000;

    memcpy(p_peer_addr.addr, peer_addr, 6);
    ret = ecr_ble_gap_connect(&p_peer_addr, &conn_param);
    BLE_COMMAND_CHECK_RETURN(ret != BT_ERROR_NO_ERROR, ret, "start conn fail");

    return BT_ERROR_NO_ERROR;
}

void ble_spi_start_disconnect()
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ecr_ble_gap_disconnect(priv->conn_handler, BT_ERROR_CON_TERM_BY_LOCAL_HOST);
}

void ble_spi_get_self_addr()
{
    ECR_BLE_GAP_ADDR_T dev_addr = {0};
    /*data[0]: get addr status*/
    uint8_t send_data[7];
    memset(send_data, 0x00, 7);

    send_data[0] = ecr_ble_get_device_addr(&dev_addr);
    memcpy(&send_data[1], dev_addr.addr, 6);
    uint8_t type = BLE_SPI_BUILD_TYPE(BLE_SPI_TYPE_EVENT, BLE_SPI_TYPE_EVENT_SUBTYPE_GETADDR);
    ble_spi_send_event(type, send_data, 7);
}

void ble_spi_gattc_exchange_mtu_req(uint16_t mtu_val)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ecr_ble_gattc_exchange_mtu_req(priv->conn_handler, mtu_val);
}

void ble_spi_gattc_write(uint16_t char_handle, uint16_t data_len, uint8_t *data)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ecr_ble_gattc_write(priv->conn_handler, char_handle, data, data_len);
}

void ble_spi_gattc_read(uint16_t char_handle)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ecr_ble_gattc_read(priv->conn_handler, char_handle);
}

void ble_spi_gatts_notify(uint8_t *data, uint16_t data_len)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    ecr_ble_gatts_value_notify(priv->conn_handler, priv->ntf_ind_handler, data, data_len);
}

int ble_spi_send_msg(ble_spi_msg_t *msg)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();

    if (priv->msg_queue) {
        return os_queue_send(priv->msg_queue, (char *)msg, sizeof(ble_spi_msg_t), 0);
    } else {
        return -1;
    }
}

void ble_apps_init(void)
{
    ble_spi_priv_t *priv = ble_spi_get_priv();
    priv->task_handle = os_task_create( "ble_apps_task", BLE_APPS_PRIORITY, BLE_APPS_STACK_SIZE, (task_entry_t)ble_spi_task, NULL);
    priv->msg_queue = os_queue_create("ble_spi_msg_queue", 10, sizeof(ble_spi_msg_t), 0);
}


