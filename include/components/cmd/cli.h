

#ifndef _CLI_H_
#define _CLI_H_

#include "easyflash.h"

#include <stdlib.h>


#include <stdarg.h>
#include <string.h>
#include "uart.h"
#include "oshal.h"
#include "cmd.h"

#ifdef CONFIG_CLI
#define CMD_CLI_USE_DESC	1

#define	CMD_CLI_RET_SUCCESS 	CMD_RET_SUCCESS
#define	CMD_CLI_RET_FAILURE 	CMD_RET_FAILURE
#define	CMD_CLI_RET_UNHANDLED 	2
#define	CMD_CLI_RET_USAGE  		CMD_RET_USAGE

#define	CMD_CLI_ATTR_TOP 		(0)
#define	CMD_CLI_ATTR_SUB 		(1)
#define	E_AMT 				(2)
#define	E_STANDALONE 		 0x4 		// 2
#define	E_TRANSPORT 		 0x8			// 3
#define	E_LMAC_TEST 		 0x10			// 4
#define	E_AT  				0x20				// 5
#define	E_ALL 		E_AMT|E_STANDALONE|E_TRANSPORT|E_LMAC_TEST|E_AT

#define CLI_PRINT_BUFFER_SIZE	512

typedef struct _CMD_CLI_DEV
{
       char cmd_dev_open;
       char cmd_echo;
       unsigned short cmd_buff_index;
       char cmd_buff_base[CMD_CORE_CMD_BUFF_MAX_LEN];
       unsigned int cmd_line;
       char cmd_newline_flag;
       char cmd_print_prefix_enable;
       char cmd_print_time_enable;
       char cmd_print_buff[CMD_CORE_LOG_BUFF_MAX_LEN];
       CMD_CORE_COMM * cmd_core;
       CMD_HAL_DEV_OPS * cmd_dev_ops;
} CMD_CLI_DEV;

typedef struct cli_cmd_f
{
	bool used;
} cmd_tbl_f_t;

typedef struct cmd_cli
{
	const char *name;
	int (*handler)(int argc, char *argv[]);
	int flag;
#if CMD_CLI_USE_DESC
	const char *desc;
	const char *usage;
#endif
	cmd_tbl_f_t * f;
} cmd_cli_t;


#define cli_cmd_declare(cmd)						\
	struct cmd_cli _cli_cmd_2_##cmd			\
	__attribute__((used, section(".cli_cmd_2_"#cmd), aligned(4)))




#define cli_subcmd_declare(cmd, subcmd)					\
	struct cmd_cli _cli_cmd_2_##cmd##_2_##subcmd	\
	__attribute__((used, 						\
		       section(".cli_cmd_2_"#cmd"_2_"#subcmd), aligned(4)))


#if CMD_CLI_USE_DESC

#define CLI_CMD(cmd, fn, d, u)				\
		cli_cmd_declare(cmd) = {				\
			.name = #cmd,				\
			.handler = fn, 				\
			.desc = d, 				\
			.usage = u,				\
			.flag = CMD_CLI_ATTR_TOP | E_ALL,	\
		}

#define CLI_CMD_M(cmd, fn, d, u, m)				\
			cli_cmd_declare(cmd) = {				\
				.name = #cmd,				\
				.handler = fn,				\
				.desc = d,				\
				.usage = u, 			\
				.flag = CMD_CLI_ATTR_TOP | m,	\
			}
#define CLI_CMD_F(cmd, fn, d, u, flage)	\
			struct cli_cmd_f _cli_cmd_2_##cmd##_f = {	\
				.used = flage,						\
			};											\
			cli_cmd_declare(cmd) = {				\
				.name = #cmd,				\
				.handler = fn,				\
				.desc = d,				\
				.usage = u, 			\
				.flag = CMD_CLI_ATTR_TOP | E_ALL,	\
				.f = &_cli_cmd_2_##cmd##_f, \
			}



#define CLI_SUBCMD(cmd, subcmd,fn, d, u)			\
		cli_subcmd_declare(cmd, subcmd) = { 		\
			.name = #subcmd,			\
			.handler = fn,				\
			.desc = d,				\
			.usage = u, 			\
			.flag = CMD_CLI_ATTR_SUB,	\
		}

#define CLI_SUBCMD_F(cmd, subcmd,fn, d, u, flage)			\
		struct cli_cmd_f _cli_cmd_2_##cmd##_2_##subcmd##_f = {	\
			.used = flage,						\
		};														\
		cli_subcmd_declare(cmd, subcmd) = { 		\
			.name = #subcmd,			\
			.handler = fn,				\
			.desc = d,				\
			.usage = u, 			\
			.flag = CMD_CLI_ATTR_SUB,	\
			.f = &_cli_cmd_2_##cmd##_2_##subcmd##_f,\
		}

#else

#define CLI_CMD(cmd, fn, d, u)				\
		cli_cmd_declare(cmd) = {				\
			.name = #cmd,				\
			.handler = fn, 				\
			.flag = CMD_CLI_ATTR_TOP|E_ALL,	\
		}

#define CLI_CMD_M(cmd, fn, d, u, m)				\
					cli_cmd_declare(cmd) = {				\
						.name = #cmd,				\
						.handler = fn,				\
						.flag = CMD_CLI_ATTR_TOP | m,	\
					}
					
#define CLI_CMD_F(cmd, fn, d, u, flage)	\
			struct cli_cmd_f _cli_cmd_2_##cmd##_f = {	\
				.used = flage,						\
			};											\
			cli_cmd_declare(cmd) = {				\
				.name = #cmd,				\
				.handler = fn,				\
				.flag = CMD_CLI_ATTR_TOP|E_ALL,				\
				.f = &_cli_cmd_2_##cmd##_f, \
			}

#define CLI_SUBCMD(cmd, subcmd,fn, d, u)			\
		cli_subcmd_declare(cmd, subcmd) = { 		\
			.name = #subcmd,			\
			.handler = fn,				\
			.flag = CMD_CLI_ATTR_SUB,	\
		}

#define CLI_SUBCMD_F(cmd, subcmd,fn, d, u, flage)			\
		struct cli_cmd_f _cli_cmd_2_##cmd##_2_##subcmd##_f = {	\
			.used = flage,						\
		};														\
		cli_subcmd_declare(cmd, subcmd) = { 		\
			.name = #subcmd,			\
			.handler = fn,				\
			.flag = CMD_CLI_ATTR_SUB,	\
			.f = &_cli_cmd_2_##cmd##_2_##subcmd##_f,\
		}

#endif

#define CLI_CMD_GET(cmd) &_cli_cmd_2_##cmd
#define CLI_SUBCMD_GET(cmd, subcmd) &_cli_cmd_2_##cmd##_2_##subcmd

#define cli_cmd_start()							\
({									\
	static char start[0] __aligned(4) __attribute__((used,	\
		section(".cli_cmd_1")));			\
	(struct cmd_cli *)&start;					\
})


#define cli_cmd_end()							\
({									\
	static char end[0] __aligned(4) __attribute__((used,		\
		section(".cli_cmd_3")));				\
	(struct cmd_cli *)&end;						\
})


#define cli_subcmd_start(cmd)						\
({									\
	static char start[0] __aligned(4) __attribute__((used,	\
		section(".cli_cmd_2_"#cmd"_1")));			\
	(struct cmd_cli *)&start;					\
})

#define cli_subcmd_end(cmd)						\
({									\
	static char end[0] __aligned(4) __attribute__((used,		\
		section(".cli_cmd_2_"#cmd"_3")));			\
	(struct cmd_cli *)&end;						\
})



void cli_cmd_deliver(char *cmd);	
void cli_amt_mode(void);
void cli_echo_set(char i);
char cli_echo_get(void);
void cli_print_prefix_change(void);
void cli_printf(const char *f, ...);
void cli_vprintf(const char *f,  va_list ap);
int component_cli_init(E_DRV_UART_NUM uart_num);

#else



#define cli_amt_mode()		do { } while (0)
#define cli_echo_set(i)		do { } while (0)
#define cli_echo_get()		do { } while (0)
#define cli_print_prefix_change()		do { } while (0)

#define cli_printf(f,...)		do { } while (0)
#define cli_vprintf(...)		do { } while (0)


#define component_cli_init(...)		do { return 0;} while (0)



#endif

#endif
