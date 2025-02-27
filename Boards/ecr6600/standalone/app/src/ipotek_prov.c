/**
* @file ipotek_proc.c
* @author 小王同学
* @brief ipotek_proc module is used to 
* @version 0.1
* @date 2023-04-27
*
*/
#include <string.h>
#include "ipotek_prov.h"
#include "easyflash.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
USER_PROV_INFO_T user_prov_info;

/***********************************************************
***********************typedef define***********************
***********************************************************/
 
 
/***********************************************************
***********************variable define**********************
***********************************************************/
 
 
/***********************************************************
***********************function define**********************
***********************************************************/
sys_err_t ipotek_prov_data_init(void)
{
    int len = 0;
    sys_err_t op_ret = SYS_OK;
    memset(&user_prov_info, 0, sizeof(user_prov_info));

    // 获取设备配网信息
    customer_get_env_blob("ssid", (void *)user_prov_info.ssid, sizeof(user_prov_info.ssid), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read ssid error\r\n");
    }

    customer_get_env_blob("password", (void *)user_prov_info.password, sizeof(user_prov_info.password), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read password error\r\n");
    }

    customer_get_env_blob("channel", (void *)&user_prov_info.channel, sizeof(user_prov_info.channel), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read channel error\r\n");
    }

    customer_get_env_blob("auth_mode", (void *)&user_prov_info.auth_mode, sizeof(user_prov_info.auth_mode), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read auth_mode error\r\n");
    }

    customer_get_env_blob("user_key", (void *)&user_prov_info.user_key, sizeof(user_prov_info.user_key), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read user_key error\r\n");
    }

    customer_get_env_blob("gw_address", (void *)&user_prov_info.gw_address, sizeof(user_prov_info.gw_address), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read gw_address error\r\n");
    }

    customer_get_env_blob("gw_address2", (void *)&user_prov_info.gw_address2, sizeof(user_prov_info.gw_address2), &len);
    if (0 == len) {
        os_printf(LM_APP, LL_ERR, "read gw_address2 error\r\n");
    }

    os_printf(LM_APP, LL_INFO, "read ssid:%s\r\n", user_prov_info.ssid);
    os_printf(LM_APP, LL_INFO, "read password:%s\r\n", user_prov_info.password);
    os_printf(LM_APP, LL_INFO, "read user_key:%s\r\n", user_prov_info.user_key);
    os_printf(LM_APP, LL_INFO, "read gw_address:%s\r\n", user_prov_info.gw_address);
    os_printf(LM_APP, LL_INFO, "read gw_address2:%s\r\n", user_prov_info.gw_address2);
    os_printf(LM_APP, LL_INFO, "read channel:%d\r\n", user_prov_info.channel);
    os_printf(LM_APP, LL_INFO, "read auth_mode:%d\r\n", user_prov_info.auth_mode);

    return op_ret;
}

sys_err_t ipotek_prov_data_save(USER_PROV_INFO_T *data)
{
    EfErrCode ef_err = EF_NO_ERR;
    strcpy((char *)&user_prov_info.ssid, (const char *)&data->ssid);
    strcpy((char *)&user_prov_info.password, (const char *)&data->password);
    strcpy((char *)&user_prov_info.user_key, (const char *)&data->user_key);
    strcpy((char *)&user_prov_info.gw_address, (const char *)&data->gw_address);
    strcpy((char *)&user_prov_info.gw_address2, (const char *)&data->gw_address2);
    user_prov_info.channel = data->channel;
    user_prov_info.auth_mode = data->auth_mode;

    os_printf(LM_APP, LL_ERR, "---->user_prov_info.ssid:%s\r\n", user_prov_info.ssid);
    os_printf(LM_APP, LL_ERR, "---->user_prov_info.password:%s\r\n", user_prov_info.password);
    // 设置设备配网信息
    ef_err = customer_set_env_blob("ssid", (void *)user_prov_info.ssid, sizeof(user_prov_info.ssid));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save ssid error\r\n");
    }

    ef_err = customer_set_env_blob("password", (void *)user_prov_info.password, sizeof(user_prov_info.password));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save password error\r\n");
    }

    ef_err = customer_set_env_blob("channel", (void *)&user_prov_info.channel, sizeof(user_prov_info.channel));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save channel error\r\n");
    }

    ef_err = customer_set_env_blob("auth_mode", (void *)&user_prov_info.auth_mode, sizeof(user_prov_info.auth_mode));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save auth_mode error\r\n");
    }

    ef_err = customer_set_env_blob("user_key", (void *)&user_prov_info.user_key, sizeof(user_prov_info.user_key));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save user_key error\r\n");
    }

    ef_err = customer_set_env_blob("gw_address", (void *)&user_prov_info.gw_address, sizeof(user_prov_info.gw_address));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save gw_address error\r\n");
    }

    ef_err = customer_set_env_blob("gw_address2", (void *)&user_prov_info.gw_address2, sizeof(user_prov_info.gw_address2));
    if (EF_NO_ERR != ef_err) {
        os_printf(LM_APP, LL_ERR, "save gw_address2 error\r\n");
    }

    return (sys_err_t)ef_err;
}

sys_err_t prov_get_ssid(char* ssid)
{
    sys_err_t op_ret = SYS_ERR;
    int len = strlen(user_prov_info.ssid);
    if (len !=0) {
        strcpy(ssid, (const char *)&user_prov_info.ssid);
        op_ret = SYS_OK;
    }
    return op_ret;
}

sys_err_t prov_get_password(char *password)
{
    sys_err_t op_ret = SYS_ERR;
    int len = strlen(user_prov_info.password);
    if (len !=0) {
        strcpy(password, (const char *)&user_prov_info.password);
        op_ret = SYS_OK;
    }
    return op_ret;
}

sys_err_t prov_get_gw_address2(char *gw_address2)
{
    sys_err_t op_ret = SYS_ERR;
    int len = strlen(user_prov_info.gw_address2);
    if (len !=0) {
        strcpy(gw_address2, (const char *)&user_prov_info.gw_address2);
        op_ret = SYS_OK;
    }
    return op_ret;
}

sys_err_t prov_get_user_key(char *user_key)
{
    sys_err_t op_ret = SYS_ERR;
    int len = strlen(user_prov_info.user_key);
    if (len != 0) {
        strcpy(user_key, (const char *)&user_prov_info.user_key);
        op_ret = SYS_OK;
    }
    return op_ret;
}

sys_err_t prov_get_wifi_channel(unsigned char *channel)
{
    *channel = user_prov_info.channel;
    return SYS_OK;
}
