#ifndef _CMD_CORE_COMM_H_
#define _CMD_CORE_COMM_H_
#include "cli.h"
#define ESCAPE_KEY 			0x1b
#define BACKSP_KEY 			0x08
#define TAB_KEY    			0x09
#define RETURN_KEY 			0x0D
#define DELETE_KEY 			0x7F
#define BELL       			0x07

CMD_CORE_COMM *cmd_core_init(void);
void cmd_core_inform_callback(void);
void cli_core_inform_callback(void);

#endif
