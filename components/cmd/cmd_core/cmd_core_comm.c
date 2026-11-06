
#include <stdlib.h>

#ifdef CONFIG_CMD_AT
#include "at.h"
#include "cmd_core_at.h"
#endif

#ifdef CONFIG_CLI
#include "cli.h"
#include "cmd_core_cli.h"
#endif

#include "cmd_core_comm.h"






static CMD_CORE_COMM s_cmd_core_dev = {0};
extern int g_in_amt_flag;

void cmd_task(void *arg)
{
#ifdef CONFIG_CMD_AT
    while(1) {
        os_sem_wait(s_cmd_core_dev.cmd_sem, WAIT_FOREVER);
        if (!g_in_amt_flag) {
            cmd_at_buf_handle();
        }
    }
#endif
}

void cli_task(void *arg)
{
#ifdef CONFIG_CLI
    while(1) {
        os_sem_wait(s_cmd_core_dev.cli_sem, WAIT_FOREVER);
        cmd_cli_buf_handle();
    }
#endif
}

void cmd_core_inform_callback(void)
{
	os_sem_post(s_cmd_core_dev.cmd_sem);
}

void cli_core_inform_callback(void)
{
	os_sem_post(s_cmd_core_dev.cli_sem);
}

CMD_CORE_COMM *cmd_core_init(void)
{
    CMD_CORE_COMM * p_dev = &s_cmd_core_dev;

    if (!p_dev->cmd_sem) {
        p_dev->cmd_sem = os_sem_create(1, 0);
        p_dev->cli_sem = os_sem_create(1, 0);
    } else {
        return p_dev;
    }

    if (!p_dev->cmd_task_handle) {
        p_dev->cmd_task_handle = os_task_create("cmd-task", FHOST_CLI_PRIORITY, \
            FHOST_CLI_STACK_SIZE, (task_entry_t)cmd_task, NULL);
    }

    if (!p_dev->cli_task_handle) {
        p_dev->cli_task_handle = os_task_create("cli-task", FHOST_CLI_PRIORITY + 1, \
            FHOST_CLI_STACK_SIZE, (task_entry_t)cli_task, NULL);
    }

	return p_dev;
}

int cmd_core_get_arg_digit (unsigned char index, int * value)
{
	CMD_CORE_ARG * p_cmd_core_arg = &s_cmd_core_dev.cmd_arg;
	long long tmpvalue;

	if (value)
	{
		char *endptr;

		tmpvalue = strtoll(p_cmd_core_arg->cmd_argv[index], &endptr, 10);
		if (*endptr != '\0')
		{
            char *digit = p_cmd_core_arg->cmd_argv[index];
	        if (!(digit[0] == '0' && (digit[1] == 'x' || digit[1] == 'X'))) {
                return CMD_RET_EVAL;
            }
			tmpvalue = strtoll(p_cmd_core_arg->cmd_argv[index], &endptr, 16);
			if (*endptr != '\0')
			{
				return CMD_RET_EVAL;
			}
		}

		if (tmpvalue > 4294967295LL) {
			return CMD_RET_EVAL;
		}

		*value = tmpvalue;

		return CMD_RET_SUCCESS;
	}

	return CMD_RET_FAILURE;
}

int cmd_core_get_arg_str (unsigned char index, char ** pStr)
{
	CMD_CORE_ARG * p_cmd_core_arg = &s_cmd_core_dev.cmd_arg;

	if (pStr)
	{
		*pStr = p_cmd_core_arg->cmd_argv[index];
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_FAILURE;
}
