/**
 * @file filename
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-5-1
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
*												Include files
--------------------------------------------------------------------------*/
#include "system_wifi.h"
#include "system_queue.h"
#include "cli.h"

/****************************************************************************
* 	                                           Local Macros
****************************************************************************/


/****************************************************************************
* 	                                           Local Types
****************************************************************************/

/****************************************************************************
* 	                                           Local Constants
****************************************************************************/

/****************************************************************************
* 	                                           Local Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Global Constants
****************************************************************************/

/****************************************************************************
* 	                                          Global Variables
****************************************************************************/

/****************************************************************************
* 	                                          Global Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Function Definitions
****************************************************************************/

#ifdef CONFIG_AWS_APP
extern void aws_app_main(void);
void aws_mqtt_main_thread(void* arg)
{
    extern bool is_wifi_conn;
    os_printf(LM_CMD, LL_INFO, "aws mqtt task started!\n");
    while (!is_wifi_conn)
    {
        os_msleep(2*1000);
        os_printf(LM_CMD, LL_INFO, "Please connect to the network!\n");
    }
    aws_app_main();
}
#endif

int aws_mqtt_test(int argc, char *argv[])
{
    
#ifdef CONFIG_AWS_APP
    os_task_create("aws_mqtt_main", 5, 4096, aws_mqtt_main_thread, NULL);
#endif
    return CMD_RET_SUCCESS;
}
CLI_CMD(aws_mqtt_test,aws_mqtt_test, "aws test",    "aws test");

