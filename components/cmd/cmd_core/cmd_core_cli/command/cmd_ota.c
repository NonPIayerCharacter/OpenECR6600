/**
 * @file filename
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-4-22
 * @version V0.1
 * @par Copyright by http://eswin.com/.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */

/*--------------------------------------------------------------------------
 *                                 Include files
 *--------------------------------------------------------------------------*/
#include "ota.h"
#include "http_ota.h"
#include "oshal.h"
#include "cli.h"
#include "easyflash.h"
#include "mqtt_ota.h"

/*--------------------------------------------------------------------------
 *                                 Local Macros
 *--------------------------------------------------------------------------*/
#define CMD_OTA_TASK_PRIORITY   (5)
#define CMD_OTA_TASK_STACK_SIZE (4 * 1024)

/*--------------------------------------------------------------------------
* 	                                           	Local Types
--------------------------------------------------------------------------*/
/**
 * @brief The brief description
 * @details The detail description
 */

/*--------------------------------------------------------------------------
* 	                                           	Local Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                           	Local Function Prototypes
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Global Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Global Variables
--------------------------------------------------------------------------*/
/**  Description of global variable  */

/*--------------------------------------------------------------------------
* 	                                          	Global Function Prototypes
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Function Definitions
--------------------------------------------------------------------------*/

int cmd_get_sn(int argc, char *argv[])
{
	char value[OTA_SN_LEN];
	char ret = 0;

	memset(value, 0, OTA_SN_LEN);
	
	if((ret = amt_get_env_blob(OTA_SN, value, OTA_SN_LEN, NULL) ) == 0)
	{
		os_printf(LM_CMD, LL_INFO, "NO %s!!\n", OTA_SN);
		return CMD_RET_FAILURE;
	}
	if(ret == 0xffffffff)
		return CMD_RET_FAILURE;

	os_printf(LM_CMD, LL_INFO, "%s is %s\n", OTA_SN, value);
	return CMD_RET_SUCCESS;
}
CLI_CMD(get_sn, cmd_get_sn, "get eswin sn", "get_sn");



int cmd_set_sn(int argc, char *argv[])
{	
	int ret;
	char *value;

	if(argc != 2)
	{
		os_printf(LM_CMD, LL_INFO, "SET ARG NUM(: %d) IS ERROR!!\n", argc);
		return CMD_RET_FAILURE;
	}

	value = argv[1];

	ret = amt_set_env_blob(OTA_SN, value, strlen(value));
	
	if(ret)
	{
		if(ret != EF_WRITE_ERR)
			os_printf(LM_CMD,LL_INFO, "SET ERROR(%d)!!\n", ret);
		return CMD_RET_FAILURE;
	}

	os_printf(LM_CMD,LL_INFO, "%s is %s\n", OTA_SN, value);
	return CMD_RET_SUCCESS;
}
CLI_CMD(set_sn, cmd_set_sn, "set eswin sn", "set_sn 123456");



int cmd_get_project_id(int argc, char *argv[])
{	
	char value[OTA_PROJECT_ID_LEN];
	char ret = 0;

	memset(value, 0, OTA_PROJECT_ID_LEN);
	
	if((ret = amt_get_env_blob(OTA_PROJECT_ID, value, OTA_PROJECT_ID_LEN, NULL) )== 0)
	{
		os_printf(LM_CMD,LL_INFO, "NO %s!!\n", OTA_PROJECT_ID);
		return CMD_RET_FAILURE;
	}
	if(ret == 0xffffffff)
		return CMD_RET_FAILURE;

	os_printf(LM_CMD,LL_INFO, "%s is %s\n", OTA_PROJECT_ID, value);
	return CMD_RET_SUCCESS;
}
CLI_CMD(get_project_id, cmd_get_project_id, "get eswin project id", "get_project_id");



int cmd_set_project_id(int argc, char *argv[])
{	
	int ret;
	char *value;

	if(argc != 2)
	{
		os_printf(LM_CMD,LL_INFO, "SET ARG NUM(: %d) IS ERROR!!\n", argc);
		return CMD_RET_FAILURE;
	}

	value = argv[1];

	ret = amt_set_env_blob(OTA_PROJECT_ID, value, strlen(value));
	
    if (ret) {
		if(ret != EF_WRITE_ERR)
			os_printf(LM_CMD,LL_INFO, "SET ERROR(%d)!!\n", ret);
		return CMD_RET_FAILURE;
	}

	os_printf(LM_CMD,LL_INFO, "%s is %s\n", OTA_PROJECT_ID, value);
	return CMD_RET_SUCCESS;
}
CLI_CMD(set_project_id, cmd_set_project_id, "set eswin project id", "set_project_id led");

/**
 * @brief OTA test service function
 * @param arg Download URL argument
 */
static bool g_ota_start = 0;
static void cmd_service_ota_test(void *arg)
{
    char *url = (char *)arg;
    int ret;

    ret = http_client_download_file(url, true);
    os_free(url);
    g_ota_start = false;

    if (ret != 0) {
        os_printf(LM_CMD, LL_ERR, "OTA:download failed\n");
    }
    os_task_delete(0);
}
/**
 * @brief Start OTA service command
 * @param h Command handler
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command execution result
 */
int cmd_service_ota_start(int argc, char *argv[])
{
    char *url;
    int len;
    int task_handle;

    if (argc != 2) {
        os_printf(LM_CMD, LL_INFO, "%s url\n", argv[0]);

        return CMD_RET_FAILURE;
    }
    if (g_ota_start) {
        os_printf(LM_CMD, LL_WARN, "OTA:download already in progress\n");

        return CMD_RET_FAILURE;
    }
    if (ota_is_ab_update_pending()) {
        os_printf(LM_CMD, LL_ERR, "OTA:AB update pending confirm, only rollback or confirm is allowed\n");

        return CMD_RET_FAILURE;
    }
    len = strlen(argv[1]);

    url = os_zalloc(len + 1);
    if(url == NULL) {
        os_printf(LM_CMD, LL_INFO, "%s mem not enough\n", argv[0]);

        return CMD_RET_FAILURE;
    }
    memcpy(url, argv[1], len);

    g_ota_start = true;
    task_handle = os_task_create("cmd-ota-task", CMD_OTA_TASK_PRIORITY,
                                 CMD_OTA_TASK_STACK_SIZE, cmd_service_ota_test, url);
    if (task_handle < 0) {
        g_ota_start = false;
        os_free(url);
        os_printf(LM_CMD, LL_ERR, "OTA:create download task failed\n");

        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ota, service_ota_start, cmd_service_ota_start, "ota start", "ota service_ota_start");

/**
 * @brief Change active partition command
 * @param h Command handler
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command execution result
 */
int cmd_change_active_part(int argc, char *argv[])
{
    if (change_active_part_for_ab() != 0) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ota, change_active_part, cmd_change_active_part,
           "Modifying active partitions during AB upgrade", "ota change_active_part");

/**
 * @brief Confirm OTA update command
 * @param h Command handler
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command execution result
 */
int cmd_ota_confirm_update(int argc, char *argv[])
{
    if (ota_confirm_update() != 0) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ota, ota_confirm_update, cmd_ota_confirm_update,
           "ota confirm update new partitions", "ota ota_confirm_update");

CLI_CMD(ota, NULL, "ota cmd", "ota <sub cmd>");
