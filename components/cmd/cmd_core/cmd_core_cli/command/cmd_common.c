/**
 * @file cmd_common.c
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-6-8
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
	
#include "cli.h"

/*--------------------------------------------------------------------------
* 	                                           	Local Macros
--------------------------------------------------------------------------*/
/** Description of the macro */

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
static int cli_echo_close(int argc, char *argv[])
{
	char i;
	if (argc >= 2)
	{
		i =  (char)strtoul(argv[1], NULL, 0);
	}
	else
	{
		os_printf(LM_CMD, LL_ERR, "error: no enough argc!\r\n");
		return 0;
	}
	if(i != 0 && i != 1)
	{
		os_printf(LM_CMD, LL_ERR, "input error!\r\n");
		return CMD_RET_FAILURE;
	}

	cli_echo_set(i);

	return CMD_RET_SUCCESS;
}
CLI_CMD(echoclose, cli_echo_close, "close echo", "echoclose [i]");


static int cli_echo_close_read(int argc, char *argv[])
{

	os_printf(LM_CMD, LL_INFO, "%d,", cli_echo_get());		

	return CMD_RET_SUCCESS;
}
CLI_CMD(echoread, cli_echo_close_read, "echoread", "echoread");


int cmd_log_prefix(int argc, char *argv[])
{

	cli_print_prefix_change();
	
	return CMD_RET_SUCCESS;
}
CLI_CMD(log_prefix, cmd_log_prefix,  "change log prefix",    "log_prefix");


char log_level_str[LL_MAX+1][8] = {"NO", "ASSERT", "ERROR", "WARNING", "INFO", "DEBUG", "ALL"};
extern struct log_ctrl_map g_log_map[LM_MAX];
static int subcmd_level_info(int argc, char *argv[])
{
	os_printf(LM_CMD, LL_NO, "\n");
	for(int i=0; i<LM_MAX; i++)
	{
		os_printf(LM_CMD, LL_NO, "%-6s :%d - %s\n", g_log_map[i].mod_name, g_log_map[i].level, log_level_str[g_log_map[i].level]);
	}

	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(log, level_info, subcmd_level_info, "show all level", "log level_info");


static int subcmd_set_level(int argc, char *argv[])
{
	char * mode;
	char * level;
	if(argc != 3)
	{
		os_printf(LM_CMD, LL_ERR, "ARG NUM(: %d) IS ERROR! Need 3!\n", argc);
		return CMD_RET_FAILURE;
	}

	level = argv[2];
	int t_level = -1;
	if(strlen(level) == 1 && '0' <= *level && *level <= '0'+LM_MAX)
	{
		t_level = atoi(level);
	}
	else
	{
		for(int i=0; i<LL_MAX; i++)
		{
			if(strcmp(log_level_str[i], strupr(level))==0)
			{
				t_level = i;
				break;
			}
		}
		if(-1 == t_level)
		{
			os_printf(LM_CMD, LL_INFO, "err\n");
			return CMD_RET_FAILURE;
		}
	}

	mode = argv[1];
	if(strcmp("ALL", strupr(mode))==0 )
	{
		for(int i=0; i<LM_MAX; i++)
		{
			g_log_map[i].level = t_level;
		}
	}
	else
	{
		int t_mode = -1;
		for(int i=0; i<LM_MAX; i++)
		{
			if(strcmp(g_log_map[i].mod_name, strlwr(mode))==0)
			{
				t_mode = i;
				break;
			}
		}
		if(-1 == t_mode)
		{
			os_printf(LM_CMD, LL_INFO, "err\n");
			return CMD_RET_FAILURE;
		}

		g_log_map[t_mode].level = t_level;
	}

	subcmd_level_info(argc, argv);
	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(log, set_level, subcmd_set_level, "set mode log level", "log set_level <mode> <level>");

CLI_CMD(log, NULL, "log cmd", "log <subcmd>");


