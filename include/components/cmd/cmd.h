#ifndef _CMD_H
#define _CMD_H
#include "oshal.h"
#define	CMD_RET_NONE		1
#define	CMD_RET_SUCCESS 	0
#define	CMD_RET_FAILURE 	-1
#define	CMD_RET_EVAL  		-2
#define	CMD_RET_ENUM  		-3
#define	CMD_RET_USAGE  		-4

#define CMD_CORE_CMD_AT_BUFF_MAX_LEN     256
#define CMD_CORE_CMD_BUFF_MAX_LEN        256+128
#define CMD_CORE_LOG_BUFF_MAX_LEN        512
#define CMD_CORE_ARG_MAX_NUM   23
#define CMD_CORE_NAME                  1
#define CMD_CORE_ARG_TOTAL     (CMD_CORE_ARG_MAX_NUM + CMD_CORE_NAME)
typedef struct _CMD_HAL_DEV_OPS
{
       int (*getc)(void *, char *);
       int (*puts)(void *, char *, unsigned int);
} CMD_HAL_DEV_OPS;

typedef struct _CMD_CORE_ARG
{
       char * cmd_argv[CMD_CORE_ARG_TOTAL];
       unsigned char cmd_at_type;
       unsigned char cmd_arg_num;
       unsigned char cmd_rev[2];
} CMD_CORE_ARG;

typedef struct _CMD_CORE_COMM
{
    int cmd_task_handle;
    os_sem_handle_t cmd_sem;
    CMD_CORE_ARG cmd_arg;
    int cli_task_handle;
    os_sem_handle_t cli_sem;
} CMD_CORE_COMM;

int cmd_core_get_arg_digit (unsigned char index, int * value);
int cmd_core_get_arg_str (unsigned char index, char ** pStr);

#endif
