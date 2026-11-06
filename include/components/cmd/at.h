

#ifndef _AT_H_
#define _AT_H_

#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include "uart.h"
#include "oshal.h"
#include "cmd.h"


#ifdef CONFIG_CMD_AT


/*  1  AT COFNIG - ECHO    */
#define CONFIG_AT_ECHO_DEFAULT		1

/*  2  AT COFNIG - RESULT_OK    */
//#define CONFIG_AT_RESULT_SUCCESS "OK\n"
#ifndef CONFIG_AT_RESULT_SUCCESS
#define CONFIG_AT_RESULT_SUCCESS "\r\nOK\r\n"
#endif

/*  3  AT COFNIG - RESULT_FAILED    */
//#define CONFIG_AT_RESULT_FAILED "ERROR\n"
#ifndef CONFIG_AT_RESULT_FAILED
#define CONFIG_AT_RESULT_FAILED "\r\nERROR\r\n"
#endif

/*  4  AT COFNIG - SEPARATOR    */
//#define CONFIG_AT_SEPARATOR ' '
#ifndef CONFIG_AT_SEPARATOR
#define CONFIG_AT_SEPARATOR ','
#endif

/*  5  AT COFNIG - QUERY_ACION    */
//#define CONFIG_AT_QUERY_ACION_EQU

/*  6  AT COFNIG - FILTER_DOUBLE_QUOTES    */
#define CONFIG_AT_FILTER_DOUBLE_QUOTES



/* error num for at cmd result */
#define	CMD_AT_RET_ERR_MASK(X) 						((X) & 0xFFFF0000)
#define	CMD_AT_RET_ERR_MASK_SUB(X) 					((X) & 0x0000FFFF)

#define	CMD_AT_RET_SUCCESS		 					(0x01000000)

#define	CMD_AT_RET_FAILURE	 						(0x01010000)			// just error
#define	CMD_AT_RET_ERR_NON_FINISH 					(0x01020000)			// no "\r\n"
#define	CMD_AT_RET_ERR_NON_AT 						(0x01030000)			// not start with "at"
#define	CMD_AT_RET_ERR_PARA_LEN(X) 					(0x01040000 | CMD_AT_RET_ERR_MASK_SUB(X))	// length error
#define	CMD_AT_RET_ERR_PARA_TYPE(X)					(0x01050000 | CMD_AT_RET_ERR_MASK_SUB(X))	// para type error
#define	CMD_AT_RET_ERR_PARA_NUM(required)			(0x01060000 | CMD_AT_RET_ERR_MASK_SUB((required) << 8))	// para number error
#define	CMD_AT_RET_ERR_PARA_INVAL(X)				(0x01070000 | CMD_AT_RET_ERR_MASK_SUB(X))	// para invalid error
#define	CMD_AT_RET_ERR_PARA_PARSE(X)				(0x01080000 | CMD_AT_RET_ERR_MASK_SUB(X))	// parse para  failed
#define	CMD_AT_RET_ERR_UNSUPPORT					(0x01090000)		// cmd is not supported
#define	CMD_AT_RET_ERR_EXEC(X)						(0x010A0000 | CMD_AT_RET_ERR_MASK_SUB(X))	// cmd exec failed
#define	CMD_AT_RET_ERR_PROCESSING					(0x010B0000)		// pre cmd is processing now
#define	CMD_AT_RET_ERR_OP_TYPE						(0x010C0000)		// cmd operation type error
	
#define	CMD_AT_RET_UNHANDLED 						(0x02000000)
//#define	CMD_AT_RET_USAGE  							0x03000000



/* at cmd type */
#define AT_TYPE_UNKNOWN 					0x0
#define AT_TYPE_QUERY		 				0x1
#define AT_TYPE_ASSIGN						0x2

#ifdef CONFIG_AT_QUERY_ACION_EQU
#define AT_TYPE_ACTION						0x1
#else
#define AT_TYPE_ACTION						0x4
#endif
#define AT_TYPE_TEST						0x8

#define AT_TYPE_MAX						    0xF


#define VERSION_STRING "1.0"



#define AT_ARG_INDEX_0			0
#define AT_ARG_INDEX_1			1
#define AT_ARG_INDEX_2			2
#define AT_ARG_INDEX_3			3
#define AT_ARG_INDEX_4			4
#define AT_ARG_INDEX_5			5
#define AT_ARG_INDEX_6			6
#define AT_ARG_INDEX_7			7
#define AT_ARG_INDEX_8			8
#define AT_ARG_INDEX_9			9
#define AT_ARG_INDEX_10			10
#define AT_ARG_INDEX_11			11
#define AT_ARG_INDEX_12			12
#define AT_ARG_INDEX_13			13
#define AT_ARG_INDEX_14			14
#define AT_ARG_INDEX_15			15
#define AT_ARG_INDEX_16			16

#define AT_MAX_ARGS 16



typedef enum
{
	ARG_TYPE_NUMBER,
	ARG_TYPE_NUMBER_HEX,
	ARG_TYPE_STRING,
	ARG_TYPE_STRING_QUOTE,
	ARG_TYPE_BINARY,
	ARG_NOT_SPECIFIED
} arg_type_t;

typedef enum {
    AT_OPER_MODE_ORDER,
    AT_OPER_MODE_DATA,
} data_handle_type_e;

typedef struct
{
	unsigned int length;
	char * addr;
} bin_info;


typedef struct 
{
	arg_type_t type;
	union
	{
		int number;
		const char * string;
		bin_info info;
	} value;
} arg_t;


typedef struct cmd_at
{
	const char *name;
	int (*handler)(unsigned char type, unsigned char argc);
	unsigned char type;
} cmd_at_t;


typedef struct init_at
{
	const char *name;
	int (*handler)(void);
} init_at_t;


#define at_cmd_declare(cmd)						\
	struct cmd_at _at_cmd_2_##cmd			\
	__attribute__((used, section(".at_cmd_2_"#cmd), aligned(4)))

#define at_init_declare(module)						\
	struct init_at _at_init_2_##module			\
	__attribute__((used, section(".at_init_2_"#module), aligned(4)))


typedef void (*at_cb_fn)(char);
typedef void (*at_pre_write_fn)(void);

#define AT_CMD(cmd, fn, t)				\
		at_cmd_declare(cmd) = {				\
			.name = #cmd,				\
			.handler = fn, 				\
			.type = t,	\
		}

#define AT_INIT(module, fn)				\
		at_init_declare(module) = {				\
			.name = #module,				\
			.handler = fn, 				\
		}

int component_at_init_uart(E_DRV_UART_NUM uart_num);

void at_vprintf(const char *f, va_list ap);
void at_printf(const char *f, ...);
void at_show_all_cmd(void);
int at_get_arg_str_flash(unsigned char index, unsigned int * p_flash_en);
void at_register_handle_cb(at_cb_fn cb);
void at_extended_result_code_with_args(const char* command_name, const arg_t* args, size_t argc, bool arg_in_brackets);
void at_output_result_failed(void);
void at_output_result_ok(void);
int at_output_data(char * buf, unsigned int len);
void at_clear_uart_rbuf(void);
data_handle_type_e at_get_handle_mode(void);
int mcu_cmd_wakeup_atsleep(int sleepmode);

#define AT_STORE_IN_RAM			0
#define AT_STORE_IN_FLASH		1
void at_set_store_mode(unsigned char mode);
unsigned char at_get_store_mode(void);
void at_set_sys_msg(unsigned char mode);
unsigned char at_get_sys_msg(void);

#define AT_ERROR_HINT_DISABLE		0
#define AT_ERROR_HINT_ENABLE		1
void at_set_error_hint_enable(unsigned char en);
unsigned char at_get_error_hint_enable(void);
void at_set_trans_interval(unsigned short time);
unsigned short at_get_trans_interval(void);

int cmd_get_arg_digit (unsigned char index, int * value);
int cmd_get_arg_digit_check (unsigned char index, int * value, int min, int max);
int cmd_get_arg_str(unsigned char index,  char ** pStr);
int cmd_get_arg_str_check(unsigned char index, char **pStr, unsigned int min_len, unsigned int max_len);

#define AT_FILTER_CMD_SET		(0<<31)
#define AT_FILTER_CMD_CLR		(1<<31)
#define AT_FILTER_CMD_BLE		(1<<0)

void at_cfg_cmd_filter (unsigned int flags);
void mcu_cmd_wakeup_write(void);

#else


#define component_at_init(uart_num)		do {} while (0)

#endif


#endif
