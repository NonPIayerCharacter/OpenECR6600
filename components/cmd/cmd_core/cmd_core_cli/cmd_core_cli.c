
#include "cli.h"
#include "rtc.h"
#include "uart.h"
#include "telnet.h"

#include "cmd_core_comm.h"

#ifdef CONFIG_CLI

extern void *cmd_hal_uart_init(E_DRV_UART_NUM uart_num, void * cb, T_DRV_UART_CONFIG * p_config);

static CMD_CLI_DEV s_cli_dev;
static int s_cli_amt_mode;
static int g_cmd_disable = 0;

static int cmd_cli_format_actual_len(int format_ret, int remaining)
{
	if (format_ret < 0 || remaining <= 0)
	{
		return 0;
	}

	if (format_ret >= remaining)
	{
		return remaining - 1;
	}

	return format_ret;
}

int cmd_cli_parse_arg(CMD_CORE_ARG *p_cmd_arg, char *s)
{
	int run;

	if (!s || *s == '\0')
	{
		cli_printf("Command arg null\n");
		return 1;
	}
	
	p_cmd_arg->cmd_argv[0] = s;
	p_cmd_arg->cmd_arg_num = 1;

	while (1)
	{
		++s;

		if (*s == '\0')
		{
			goto out;
		}
	
		if (*s == ' ')
		{
			*s = '\0';
			break;
		}
	}

	run = CMD_CORE_ARG_MAX_NUM;

	s++;

#if 1
	while (run--)
	{
		while (*s == ' ')
		{
			s++;
		}

		if (*s == '\0')
		{
			goto out;
		}	

		p_cmd_arg->cmd_argv[p_cmd_arg->cmd_arg_num++] = s;

        if (*s == '"')
        {
            do
            {
                s++;
            } 
            while (*s && (*s != '"'));

            if (*s == '\0')
            {
                goto out;
            }

            do
            {
                s++;
            } 
            while (*s && (*s != ' '));

            if (*s == '\0')
            {
                goto out;
            }

            *s++ = '\0';
        }
        else
        {
            while (*s && (*s != ' '))
            {
                s++;
            }

            if (*s == '\0')
            {
                goto out;
            }

            *s++ = '\0';
        }
	}

#else
	while (run--)
	{
		while (*s == ' ')
		{
			s++;
		}

		if (*s == '\0')
		{
			goto out;
		}	

		p_cmd_arg->cmd_argv[p_cmd_arg->cmd_arg_num++] = s;

		while (*s && (*s != ' '))
		{
			s++;
		}

		if (*s == '\0')
		{
			goto out;
		}

		*s++ = '\0';
	}
#endif

 out:
	return 0;
}




cmd_cli_t *find_cmd(char *cmd)
{
	cmd_cli_t *t;

	for (t = cli_cmd_start(); t < cli_cmd_end(); t++)
	{
		if ((!(t->flag & CMD_CLI_ATTR_SUB)) && strcmp(cmd, t->name) == 0)
		{
			if (s_cli_amt_mode && ((t->flag & E_AMT) == 0))
			{
				return NULL;
			}
				
			return t;
		}
	}

	return NULL;
}



cmd_cli_t *find_sub_cmd(cmd_cli_t *parent, char *cmd)
{
	cmd_cli_t *t;

	for (t = parent + 1; t < cli_cmd_end() && (t->flag & CMD_CLI_ATTR_SUB); t++)
	{
		if (!strcmp(t->name, cmd))
		{
			return t;
		}
	}

	return NULL;
}



static void cmd_cli_process(CMD_CORE_ARG *p_cmd_arg)
{
	cmd_cli_t *p, *t;
	int rc = 0;
	char num = p_cmd_arg->cmd_arg_num;
	char * name = p_cmd_arg->cmd_argv[0];

	t = find_cmd(name);
	if (!t)
	{
		cli_printf("Unknown command: %s\n", name);
		return;
	}
	else if(!t->f->used)
	{
		cli_printf("%s UNUSED!!!\n", name);
		return;
	}
	else if (t->handler == NULL || ((rc = t->handler(num, p_cmd_arg->cmd_argv)) == CMD_CLI_RET_UNHANDLED))
	{
		if (num < 2)
		{
			cli_printf("%s needs sub-command\n", name);
			return;
		}

		p = t;
		t = find_sub_cmd(p, p_cmd_arg->cmd_argv[1]);
		if (t == NULL)
		{
			cli_printf("%s is not a sub-command of %s\n", p_cmd_arg->cmd_argv[1], name);
			return;
		}
		else if(!t->f->used)
		{
			cli_printf("%s %s UNUSED!!!\n", name, p_cmd_arg->cmd_argv[1]);
			return;
		}
		else if (t->handler == NULL)
		{
			cli_printf("no hanlder for %s\n", name);
		}

		rc = t->handler(--num, &p_cmd_arg->cmd_argv[1]);
	}

	switch (rc) 
	{
		case CMD_CLI_RET_SUCCESS:
			cli_printf("OK\n");
			break;
		case CMD_CLI_RET_FAILURE:
			cli_printf("ERROR\n");
			break;
#if CMD_CLI_USE_DESC
		case CMD_CLI_RET_USAGE:
			cli_printf("Usage: %s\n", t->usage);
			break;
#endif
		default:
			break;
	}
}


void  cmd_cli_handle(CMD_CLI_DEV * p_cli_dev)
{
	CMD_CORE_ARG *p_cmd_arg = &p_cli_dev->cmd_core->cmd_arg;

	if (cmd_cli_parse_arg(p_cmd_arg, p_cli_dev->cmd_buff_base))
	{
		return;
	}

	cmd_cli_process(p_cmd_arg);
}

void cmd_cli_buf_handle(void)
{
	char c;
	CMD_CLI_DEV * p_cli_dev = &s_cli_dev;
	CMD_HAL_DEV_OPS * cmd_hal_dev = p_cli_dev->cmd_dev_ops;

	while (1)
	{
		if ((cmd_hal_dev->getc)((void *)cmd_hal_dev, &c) || g_cmd_disable)
		{
			return;
		}
		
		switch (c)
		{
			case BACKSP_KEY:
				if (p_cli_dev->cmd_buff_index)
				{
					cmd_hal_dev->puts((void *)cmd_hal_dev, "\b \b", 3);
					p_cli_dev->cmd_buff_index--;
				}
				break;

			case RETURN_KEY:
				if (p_cli_dev->cmd_echo)
				{
					cmd_hal_dev->puts((void *)cmd_hal_dev, "\n", 1);
					
					if (p_cli_dev->cmd_buff_index)
					{
						p_cli_dev->cmd_newline_flag = 0;
					}
				}

				if (p_cli_dev->cmd_buff_index)
				{
					p_cli_dev->cmd_buff_base[p_cli_dev->cmd_buff_index] = '\0';
					cmd_cli_handle(p_cli_dev);
					p_cli_dev->cmd_buff_index = 0;
				}

				if(p_cli_dev->cmd_print_prefix_enable)
				{
					int len;
					struct rtc_time time;

					drv_rtc_get_time(&time);

					len = snprintf(&p_cli_dev->cmd_print_buff[0], CMD_CORE_LOG_BUFF_MAX_LEN,
						"[%02d:%02d:%02d.%03d][%d]%s:", time.hour,time.min,time.sec,time.cnt_32k/33,
						p_cli_dev->cmd_line++, PROMPT_SYMBOL);
					len = cmd_cli_format_actual_len(len, CMD_CORE_LOG_BUFF_MAX_LEN);

					if (len > 0)
					{
						cmd_hal_dev->puts((void *)cmd_hal_dev, &p_cli_dev->cmd_print_buff[0], len);
					}
					p_cli_dev->cmd_newline_flag = 1;
				}
				else
				{
					p_cli_dev->cmd_line++;
				}
				break;

			case '\n':
				break;

			default:
				if (c > 0x1F && c < 0x7F && p_cli_dev->cmd_buff_index < CMD_CORE_CMD_BUFF_MAX_LEN)
				{
					p_cli_dev->cmd_buff_base[p_cli_dev->cmd_buff_index++] = c;
					if(p_cli_dev->cmd_echo)
					{
						cmd_hal_dev->puts((void *)cmd_hal_dev, &c, 1);
					}
				}
				break;	
		}
	}
}




int component_cli_init(E_DRV_UART_NUM uart_num)
{

	CMD_CLI_DEV * p_cli_dev = &s_cli_dev;

	p_cli_dev->cmd_core = cmd_core_init();
	if (!p_cli_dev->cmd_core)
	{
		return -1;
	}

	if(E_UART_SELECT_BY_KCONFIG == uart_num)
	{
#ifdef CONFIG_CLI_UART_0
		uart_num = E_UART_NUM_0;
#endif
#ifdef CONFIG_CLI_UART_1
		uart_num = E_UART_NUM_1;
#endif
#ifdef CONFIG_CLI_UART_2
		uart_num = E_UART_NUM_2;
#endif
	}

	p_cli_dev->cmd_dev_ops = (CMD_HAL_DEV_OPS *)cmd_hal_uart_init(uart_num, (void *)cli_core_inform_callback, NULL);
	p_cli_dev->cmd_print_time_enable = 1;
	p_cli_dev->cmd_print_prefix_enable = 1;
	p_cli_dev->cmd_line = 0;
	p_cli_dev->cmd_newline_flag = 0;
	p_cli_dev->cmd_echo = 1;
	p_cli_dev->cmd_dev_open = 1;

	s_cli_amt_mode = 0;

	return 0;
}

void cli_amt_mode(void)
{
	s_cli_amt_mode = 1;
}

void cli_echo_set(char i)
{
	CMD_CLI_DEV * p_cli_dev = &s_cli_dev;

	p_cli_dev->cmd_print_prefix_enable = i;
	p_cli_dev->cmd_echo = i;
}

void cli_disable_set(void)
{
	g_cmd_disable = 1;
}

char cli_echo_get(void)
{
	return s_cli_dev.cmd_echo;
}

CMD_CLI_DEV *cli_get_dev_cfg(void)
{
    return &s_cli_dev;
}

void cli_print_prefix_change(void)
{
	CMD_CLI_DEV * p_cli_dev = &s_cli_dev;

	if(p_cli_dev->cmd_print_prefix_enable) // if open now
	{
		os_printf(LM_CMD, LL_INFO, "close log prefix\n");
		p_cli_dev->cmd_print_prefix_enable = 0;
	} 
	else
	{
		os_printf(LM_CMD, LL_INFO, "open log prefix\n");
		p_cli_dev->cmd_print_prefix_enable = 1;
	}
}


void cli_printf(const char *f, ...)
{
	if (s_cli_dev.cmd_dev_open == 0)
	{
		 return ;
	}

	va_list ap;
	va_start(ap,f);
	cli_vprintf(f, ap);
	va_end(ap);
}

void cli_vprintf(const char *f,  va_list ap)
{
	int len = 0;
	int remaining;
	int ret;
	CMD_CLI_DEV  *p_cli_dev = &s_cli_dev;

	if(p_cli_dev->cmd_dev_open == 0)
	{
		 return ;
	}
	
	if(p_cli_dev->cmd_dev_open && p_cli_dev->cmd_newline_flag == 1)
	{
		p_cli_dev->cmd_print_buff[0] = '\n';
		len = 1;
		p_cli_dev->cmd_newline_flag = 0;
		p_cli_dev->cmd_print_time_enable=1;
	}
	
	if(p_cli_dev->cmd_print_time_enable && p_cli_dev->cmd_print_prefix_enable)
	{
		struct rtc_time time;
		p_cli_dev->cmd_print_time_enable=0;
		drv_rtc_get_time(&time);
		remaining = CMD_CORE_LOG_BUFF_MAX_LEN - len;
		if (remaining > 0)
		{
			ret = snprintf(&p_cli_dev->cmd_print_buff[len], remaining,
				"[%02d:%02d:%02d.%03d]",
				time.hour, time.min, time.sec, time.cnt_32k/33);
			len += cmd_cli_format_actual_len(ret, remaining);
		}
	}

	remaining = CMD_CORE_LOG_BUFF_MAX_LEN - len;
	if (remaining > 0)
	{
		ret = vsnprintf(&p_cli_dev->cmd_print_buff[len], remaining, f, ap);
		len += cmd_cli_format_actual_len(ret, remaining);
	}

	if(len > 0 && p_cli_dev->cmd_print_buff[len-1] == 0x0a)
	{
		p_cli_dev->cmd_print_time_enable=1;
	}

	if(len > 0 && telnet_log_write(p_cli_dev->cmd_print_buff, len) < 0)
	{
		p_cli_dev->cmd_dev_ops->puts((void *)p_cli_dev->cmd_dev_ops, p_cli_dev->cmd_print_buff, len);
	}
	
}


static int cmd_help(int argc, char *argv[])
{
	cmd_cli_t *t = NULL, *start, *end;
	CMD_CORE_ARG *p_cmd_arg = &s_cli_dev.cmd_core->cmd_arg;

	if (argc == 1)
	{
		for (t = cli_cmd_start(); t < cli_cmd_end(); t++)
		{
#if CMD_CLI_USE_DESC

			if (!t->desc || (t->flag & CMD_CLI_ATTR_SUB))
			{
				continue;
			}
			if(!t->f->used)
			{
				continue;
			}
			os_printf(LM_CMD, LL_INFO, "%-10s - %s\n", t->name, t->desc);
#else

			if (t->flag & CMD_CLI_ATTR_SUB)
			{
				continue;
			}
			if(!t->f->used)
			{
				continue;
			}

			os_printf(LM_CMD, LL_INFO, "%-10s\n", t->name);

#endif
		}

		goto ret_success;
	}

	// help cmd subcmd
	t = start = cli_cmd_start();
	end = cli_cmd_end();

	t = find_cmd(p_cmd_arg->cmd_argv[1]);
	if (!t)
	{
		return CMD_RET_FAILURE;
	}

	if (argc == 2 && ((t->flag & 0x1) == CMD_CLI_ATTR_TOP))
	{
#if CMD_CLI_USE_DESC

		os_printf(LM_CMD, LL_INFO, "%-16s - %s\n", t->name, t->desc);
		if (t->usage)
		{
			os_printf(LM_CMD, LL_INFO, "Usage: %s\n", t->usage);
		}

		for (t = t + 1; (t < end) && (t->flag & CMD_CLI_ATTR_SUB); t++)
		{
			if (!t->desc)
			{
				continue;
			}
			if(!t->f->used)
			{
				continue;
			}
			os_printf(LM_CMD, LL_INFO, "\t%-16s - %-32s \t- Usage:%s\n", t->name, t->desc, t->usage);
		}

#else

		os_printf(LM_CMD, LL_INFO, "%-16s\n", t->name);

		for (t = t + 1; (t < end) && (t->flag & CMD_CLI_ATTR_SUB); t++)
		{
			if(!t->f->used)
			{
				continue;
			}
			os_printf(LM_CMD, LL_INFO, "\t%-16s\n", t->name);
		}

#endif
		goto ret_success;
	}

	if (argc == 3)
	{
		t = find_sub_cmd(t, p_cmd_arg->cmd_argv[2]);
	}

	if (!t)
	{
		goto ret_fail;
	}

#if CMD_CLI_USE_DESC
	if (t->desc)
	{
		os_printf(LM_CMD, LL_INFO, "%s\n", t->desc);
	}

	if (t->usage)
	{
		os_printf(LM_CMD, LL_INFO, "Usage: %s\n", t->usage);
	}
#endif

ret_success:
	return CMD_CLI_RET_SUCCESS;

ret_fail:
	return CMD_CLI_RET_FAILURE;
}
CLI_CMD(help, cmd_help,  "display information about CLI commands",    "help [command]");


#if 0
static int cmd_test(int argc, char *argv[])
{
    for (int i=0; i<argc; i++)
    {
        cli_printf("[%d]%s\n", i, argv[i]);
    }

	return CMD_CLI_RET_SUCCESS;
}
CLI_CMD(test, cmd_test,  "test CLI command args",    "test [arg0] [arg1] ...");
#endif


void AmtInit()
{
#ifdef CONFIG_CUSTOM_SY
	component_cli_init(E_UART_NUM_1);
#else
	#ifdef CONFIG_AMT_UART_0
		component_cli_init(E_UART_NUM_0);
	#elif CONFIG_AMT_UART_1
		component_cli_init(E_UART_NUM_1);
	#elif CONFIG_AMT_UART_2
		component_cli_init(E_UART_NUM_2);
	#endif
#endif
    s_cli_dev.cmd_print_prefix_enable = 0;
}
#endif
