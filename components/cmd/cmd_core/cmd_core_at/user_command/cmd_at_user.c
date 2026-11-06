#include "at.h"
#include "http_ota.h"
#include "gpio.h"
#include "chip_pinmux.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "cli.h"

#define CMD_AT_USER_MCU_SLEEP           (1<<0)
#define CMD_AT_USER_BASIC_SLEEP         (1<<1)
#define CMD_AT_USER_MCU_WAKEUP_DELAY    (1<<2)
#define CMD_AT_USER_MCU_GPIO_WAKEUP     (1<<3)
#define CMD_AT_USER_AWAKE (CMD_AT_USER_MCU_SLEEP|CMD_AT_USER_BASIC_SLEEP|CMD_AT_USER_MCU_WAKEUP_DELAY|CMD_AT_USER_MCU_GPIO_WAKEUP)

extern E_DRV_UART_NUM at_uart_num;
#ifdef CONFIG_PSM_SURPORT
extern bool psm_set_psm_enable(unsigned int enable);
#endif
#define MCU_CMD_WAKELINK_SET_USERSLEEP(x) ((x) |= BIT0)
#define MCU_CMD_WAKELINK_ISSET_USERSLEEP(x) (((x) & BIT0) != 0)
#define MCU_CMD_WAKELINK_UNSET_USERSLEEP(x) ((x) &= ~BIT0)
#define MCU_CMD_WAKELINK_SET_WIFISLEEP(x) ((x) |= BIT1)
#define MCU_CMD_WAKELINK_ISSET_WIFISLEEP(x) (((x) & BIT1) != 0)
#define MCU_CMD_WAKELINK_UNSET_WIFISLEEP(x) ((x) &= ~BIT1)
#define MCU_CMD_WAKELINK_SET_DELAYTIME(x) ((x) |= BIT2)
#define MCU_CMD_WAKELINK_ISSET_DELAYTIME(x) (((x) & BIT2) != 0)
#define MCU_CMD_WAKELINK_UNSET_DELAYTIME(x) ((x) &= ~BIT2)
#define MCU_CMD_WAKELINK_SET_GPIO(x) ((x) |= BIT3)
#define MCU_CMD_WAKELINK_ISSET_GPIO(x) (((x) & BIT3) != 0)
#define MCU_CMD_WAKELINK_UNSET_GPIO(x) ((x) &= ~BIT3)

#define MCU_CMD_WAKECFG_SET_ENABLE(x) ((x) |= BIT0)
#define MCU_CMD_WAKECFG_ISSET_ENABLE(x) (((x) & BIT0) != 0)
#define MCU_CMD_WAKECFG_UNSET_ENABLE(x) ((x) &= ~BIT0)
#define MCU_CMD_WAKECFG_SET_UPSTATE(x) ((x) |= BIT1)
#define MCU_CMD_WAKECFG_ISSET_UPSTATE(x) (((x) & BIT1) != 0)
#define MCU_CMD_WAKECFG_UNSET_UPSTATE(x) ((x) &= ~BIT1)

#define MCU_PSM_CFG_WAKEUP 0
#define MCU_PSM_CFG_SLEEP  1

#define MCU_PSM_WAKEUP_GPIO 1
#define MCU_PSM_WAKEUP_UART 2
typedef struct {
    int status;
    int wakemode;
    int wakenumber;
    int wakesignal;
    int delaytime;
    int wakelink;
} mcu_psmcfg_t;

EventGroupHandle_t g_mcu_wakeup_event_gp;

#define MCU_COMMAND_CHECK_RETURN(condition, ret, logstr) \
    do { \
        if ((condition) != 0) { \
            if ((logstr) != NULL) { \
                os_printf(LM_APP, LL_ERR, "%s[%d]%s, ret %d\n", __FUNCTION__, __LINE__, logstr, ret); \
            } \
            return ret; \
        } \
    } while (0)

mcu_psmcfg_t g_mcupsmcfg;

#define CMD_AT_USER_RAM_FREE		0
#define CMD_AT_USER_RAM_ALLOC		1
#define CMD_AT_USER_RAM_WRITE		2
#define CMD_AT_USER_RAM_READ		3
#define CMD_AT_USER_RAM_ERASE		4

#define CMD_AT_USER_RAM_READ_MAX_SIZE		1024
#define CMD_AT_USER_RAM_READ_HEAD_SIZE		32

typedef struct _AT_USER_BUF_HEADER
{
	int buf_len;
	int wr_len;
	int offset;
	int index;
	char base[0];
} AT_USER_BUF_HEADER;

static AT_USER_BUF_HEADER * at_user_buf = NULL;

void at_user_ram_write_handle(char c)
{
	at_user_buf->base[at_user_buf->offset + at_user_buf->index++] = c;

	if (at_user_buf->index == at_user_buf->wr_len)
	{
		at_printf("WRITE OK\r\n");
		at_register_handle_cb(NULL);
	}
}

static int at_user_ram_assign (unsigned char argc)
{
	int op;
	int ret = 0;
	int temp_len;
	char * temp_buf;

	if (cmd_get_arg_digit (AT_ARG_INDEX_0, &op))
	{
		ret = 0xffff;
		goto failed;
	}

	if (op == CMD_AT_USER_RAM_WRITE || op == CMD_AT_USER_RAM_READ)
	{
		if (!at_user_buf)
		{
			ret = 1;
			goto failed;
		}
		else if (cmd_get_arg_digit (AT_ARG_INDEX_1, &at_user_buf->wr_len))
		{
			ret = 2;
			goto failed;
		}

		ret = cmd_get_arg_digit (AT_ARG_INDEX_2, &at_user_buf->offset);  
		if (ret != CMD_RET_EVAL)
		{
			if (ret == CMD_RET_ENUM || ret == CMD_RET_NONE)
			{
				at_user_buf->offset = 0;
			}

			if (at_user_buf->offset + at_user_buf->wr_len > at_user_buf->buf_len)
			{
				ret = 3;
				goto failed;
			}
			else
			{
				ret = 0;
			}
		}
		else
		{
			ret = 4;
			goto failed;
		}
	}

	switch (op)
	{
		case CMD_AT_USER_RAM_FREE:
            if (argc != 1)
            {
				ret = 12;
            }
            else if (at_user_buf || argc == 1)
			{
				os_free(at_user_buf);
				at_user_buf = NULL;
			}
			else
			{
				ret = 5;
			}
			break;

		case CMD_AT_USER_RAM_ALLOC:
			if (at_user_buf)
			{
				ret = 6;
			}
			else if (cmd_get_arg_digit (AT_ARG_INDEX_1, &temp_len))
			{
				ret = 7;
			}
			else if ((at_user_buf = os_malloc(temp_len + sizeof(AT_USER_BUF_HEADER))) == NULL)
			{
				ret = 8;
			}

			if (!ret)
			{
				at_user_buf->buf_len = temp_len;
			}
			break;

		case CMD_AT_USER_RAM_WRITE:
			at_user_buf->index = 0;
			at_printf("\r\nOK\r\n\r\n>\r\n");
			at_register_handle_cb(at_user_ram_write_handle);
			return CMD_AT_RET_UNHANDLED;

		case CMD_AT_USER_RAM_READ:
			temp_buf = os_malloc(CMD_AT_USER_RAM_READ_MAX_SIZE + CMD_AT_USER_RAM_READ_HEAD_SIZE);
			if (temp_buf == NULL)
			{
				ret = 9;
			}
			else
			{
				unsigned int read_len = 0, read_offset = 0, head_len = 0;
				int head_ret = 0;

				do
				{
					read_len = MIN(at_user_buf->wr_len - read_offset, CMD_AT_USER_RAM_READ_MAX_SIZE);
					head_ret = snprintf(temp_buf, CMD_AT_USER_RAM_READ_HEAD_SIZE, "+USERRAM:%d,", read_len);
					if (head_ret < 0)
					{
						head_len = 0;
					}
					else if (head_ret >= CMD_AT_USER_RAM_READ_HEAD_SIZE)
					{
						head_len = CMD_AT_USER_RAM_READ_HEAD_SIZE - 1;
					}
					else
					{
						head_len = head_ret;
					}
					memcpy(temp_buf + head_len, at_user_buf->base + at_user_buf->offset + read_offset, read_len);
					at_output_data(temp_buf, head_len + read_len);
					read_offset += read_len;
				} while(read_offset < at_user_buf->wr_len);

				os_free(temp_buf);
			}		
			break;

		case CMD_AT_USER_RAM_ERASE:
			if (at_user_buf)
			{
				memset(at_user_buf->base, 0, at_user_buf->buf_len);
			}
			else
			{
				ret = 10;
			}
			break;

		default:
			ret = 11;
			break;
	}

	if (ret == 0)
	{
		return CMD_AT_RET_SUCCESS;
	}

failed:
	return CMD_AT_RET_ERR_PARA_PARSE(ret);
}




static int at_user_ram (unsigned char type, unsigned char argc)
{
	if (type == AT_TYPE_QUERY)
	{
		arg_t result;
		
		result.type = ARG_TYPE_NUMBER;
		if (at_user_buf)
		{
			result.value.number = at_user_buf->buf_len;
		}
		else
		{
			result.value.number = 0;
		}

		at_extended_result_code_with_args (NULL, &result, 1, false);
	}
	else if (type == AT_TYPE_ASSIGN)
	{
		return at_user_ram_assign(argc);
	}

	return CMD_AT_RET_SUCCESS;
}
AT_CMD(USERRAM, at_user_ram , AT_TYPE_QUERY | AT_TYPE_ASSIGN);

#define CMD_AT_USER_OTA_TASK_PRI 4
#define CMD_AT_USER_OTA_TASK_DEPTH 4096

typedef struct {
    unsigned int pos;
    int urllen;
    char *url;
    os_sem_handle_t sem;
} user_ota_t;

user_ota_t g_user_ota;

user_ota_t *at_user_get_ota_priv(void)
{
    return &g_user_ota;
}

void at_user_ota_param_handle(char data)
{
    user_ota_t *priv = at_user_get_ota_priv();

    if (priv->pos < priv->urllen) {
        priv->url[priv->pos++] = data;
        return;
    }

    if (priv->pos == priv->urllen) {
        at_printf("Recv %u bytes\r\n", priv->urllen);
        os_sem_post(priv->sem);
    }
    priv->pos++;
}

static void at_user_ota_task(void *arg)
{
    user_ota_t *priv = at_user_get_ota_priv();
    int ret;

    os_sem_wait(priv->sem, portMAX_DELAY);
    os_printf(LM_APP, LL_DBG, "ota url ##%s##\n", priv->url);

    ret = http_client_download_file(priv->url, 0);
    if (ret == 0) {
        //at_printf("\nOK\n");
        at_output_result_ok();
        ota_done(1);
    } else {
        //at_printf("\nERROR\n");
        at_output_result_failed();
        ota_done(0);
    }

    os_sem_destroy(priv->sem);
    os_free(priv->url);
    at_register_handle_cb(NULL);
}

static int at_user_ota (unsigned char type, unsigned char argc)
{
    user_ota_t *priv = at_user_get_ota_priv();
    int ret;

    memset(priv, 0, sizeof(user_ota_t));

    if (argc != 1) {
        os_printf(LM_CMD, LL_ERR, "AT+USEROTA=<urllen>\n");
        return CMD_AT_RET_FAILURE;
    }

    ret = cmd_get_arg_digit_check(AT_ARG_INDEX_0, &priv->urllen, 1, 8192);
    if (ret != 0) {
        os_printf(LM_CMD, LL_ERR, "urllen should be digit, range[1,8192]\n");
        return CMD_AT_RET_FAILURE;
    }

    priv->url = os_malloc(priv->urllen + 1);
    if (priv->url == NULL) {
        os_printf(LM_CMD, LL_ERR, "cannot alloc mem(%u) for url\n", priv->urllen);
        return CMD_AT_RET_FAILURE;
    }
    memset(priv->url, 0, priv->urllen + 1);

    priv->sem = os_sem_create(1, 0);
    if (priv->sem == NULL) {
        os_free(priv->url);
        os_printf(LM_CMD, LL_ERR, "create ota sem failed\n");
        return CMD_AT_RET_FAILURE;
    }
    os_task_create("userota_task", CMD_AT_USER_OTA_TASK_PRI, CMD_AT_USER_OTA_TASK_DEPTH, at_user_ota_task, priv);
    at_register_handle_cb(at_user_ota_param_handle);

    at_printf("\r\nOK\r\n\r\n>\r\n");
    //at_printf("\n>\n");

    return CMD_AT_RET_UNHANDLED;
}
AT_CMD(USEROTA, at_user_ota, AT_TYPE_ASSIGN);

static mcu_psmcfg_t *mcu_command_get_priv(void)
{
    return &g_mcupsmcfg;
}

void mcu_cmd_wakeup_write(void)
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();
    char wakevalue;

    if (MCU_CMD_WAKECFG_ISSET_ENABLE(priv->status) == 0) {
        return;
    }

    if (MCU_CMD_WAKECFG_ISSET_UPSTATE(priv->status)) {
        return;
    }

    if (g_mcu_wakeup_event_gp == NULL) {
        return;
    }

    switch (priv->wakemode) {
        case MCU_PSM_WAKEUP_GPIO:
            drv_gpio_write(priv->wakenumber, priv->wakesignal);
#ifdef CONFIG_PSM_SURPORT
            /* if WIFI in sleep mode and sleep again, gpio value may lost */
            psm_set_psm_enable(0);
#endif
            break;

        case MCU_PSM_WAKEUP_UART:
            wakevalue = priv->wakesignal;
            at_output_data(&wakevalue, 1);
            break;

        default:
            break;
    }

    printf("wait %ums or wake-up signal\r\n", priv->delaytime);
    EventBits_t uxBits = xEventGroupWaitBits(g_mcu_wakeup_event_gp, priv->wakelink, pdFALSE, pdFALSE, priv->delaytime/portTICK_PERIOD_MS);
    if ((uxBits & priv->wakelink) == 0) {
        if (MCU_CMD_WAKELINK_ISSET_DELAYTIME(priv->wakelink)) {
            MCU_CMD_WAKECFG_SET_UPSTATE(priv->status);
        }
    } else {
        printf("get 0x%x wakeup event\r\n", uxBits);
    }

    if (priv->wakemode == MCU_PSM_WAKEUP_GPIO) {
        drv_gpio_write(priv->wakenumber, !priv->wakesignal);
#ifdef CONFIG_PSM_SURPORT
        psm_set_psm_enable(1);
#endif
    }

    return;
}

int mcu_cmd_wakeup_atsleep(int sleepmode)
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();

    MCU_COMMAND_CHECK_RETURN(MCU_CMD_WAKECFG_ISSET_ENABLE(priv->status) == 0, -1, "mcu sleep not config");
    MCU_COMMAND_CHECK_RETURN(MCU_CMD_WAKELINK_ISSET_WIFISLEEP(priv->wakelink) == 0, -1, "at sleep check bit not set");

    switch (sleepmode) {
        case 0:
            // mcu wakeup and set the command
            if (g_mcu_wakeup_event_gp)
                xEventGroupSetBits(g_mcu_wakeup_event_gp, CMD_AT_USER_BASIC_SLEEP);
            MCU_CMD_WAKECFG_SET_UPSTATE(priv->status);
            break;

        default:
            // means mcu and wifi will go sleep
            if (g_mcu_wakeup_event_gp)
                xEventGroupClearBits(g_mcu_wakeup_event_gp, CMD_AT_USER_AWAKE);
            MCU_CMD_WAKECFG_UNSET_UPSTATE(priv->status);
            break;
    }

    if (priv->wakemode == MCU_PSM_WAKEUP_GPIO) {
        drv_gpio_write(priv->wakenumber, !priv->wakesignal);
    }

    return 0;
}

static int mcu_cmd_USERWKMCUCFG(unsigned char type, unsigned char argc)
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();
    mcu_psmcfg_t cfg = {0};
    int ret, enable;

    ret = cmd_get_arg_digit_check(AT_ARG_INDEX_0, &enable, 0, 1);
    MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_0, "enable should be digit, range[0,1]");
    if (enable == 0) {
        MCU_COMMAND_CHECK_RETURN(MCU_CMD_WAKECFG_ISSET_UPSTATE(priv->status) == 0, CMD_AT_RET_FAILURE, "cannot set in mcu sleep status");
        MCU_COMMAND_CHECK_RETURN(enable == 0 && argc != AT_ARG_INDEX_1, AT_ARG_INDEX_0, "AT+USERWKMCUCFG=0");
        MCU_CMD_WAKECFG_UNSET_ENABLE(priv->status);
        return CMD_AT_RET_SUCCESS;
    }

    MCU_CMD_WAKECFG_SET_ENABLE(cfg.status);
    MCU_COMMAND_CHECK_RETURN(argc != AT_ARG_INDEX_5 && argc != AT_ARG_INDEX_6, argc, "AT+USERWKMCUCFG=1,\
        <wake mode>,<wake number>,<wake signal>, <delay time>, [,<check mcu awake method>]");
    MCU_COMMAND_CHECK_RETURN(MCU_CMD_WAKECFG_ISSET_ENABLE(priv->status), AT_ARG_INDEX_0, "mcu cfg done, please disable then setting new cfg");
    ret = cmd_get_arg_digit_check(AT_ARG_INDEX_1, &cfg.wakemode, 1, 2);
    MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_1, "wakemode should be digit, range[1,2]");

    if (cfg.wakemode == MCU_PSM_WAKEUP_GPIO) {
        ret = cmd_get_arg_digit(AT_ARG_INDEX_2, &cfg.wakenumber);
        MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_2, "gpionum should be digit");
        ret = cmd_get_arg_digit_check(AT_ARG_INDEX_3, &cfg.wakesignal, 0, 1);
        MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_3, "wake signal should be digit, range[0,1]");
    } else {
        ret = cmd_get_arg_digit_check(AT_ARG_INDEX_2, &cfg.wakenumber, at_uart_num, at_uart_num);
        MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_2, "uart should be equal at_uart_num");
        ret = cmd_get_arg_digit_check(AT_ARG_INDEX_3, &cfg.wakesignal, 0, 255);
        MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_3, "wake signal should be digit, range[0,255]");
    }

    ret = cmd_get_arg_digit_check(AT_ARG_INDEX_4, &cfg.delaytime, 0, 60000);
    MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_4, "delaytime should be digit, range[0,60000]");

    if (argc == AT_ARG_INDEX_6) {
        ret = cmd_get_arg_digit_check(AT_ARG_INDEX_5, &cfg.wakelink, 0, 7);
        MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_5, "wakelink should be digit, range[0,7]");
    } else {
        MCU_CMD_WAKELINK_SET_USERSLEEP(cfg.wakelink);
    }

    if (cfg.wakemode == MCU_PSM_WAKEUP_GPIO) {
		set_gpio_function(cfg.wakenumber);
		drv_gpio_ioctrl(cfg.wakenumber, DRV_GPIO_CTRL_SET_DIRCTION, DRV_GPIO_ARG_DIR_OUT);
        drv_gpio_write(cfg.wakenumber, !cfg.wakesignal);
    }

    enable = priv->status;
    memcpy(priv, &cfg, sizeof(cfg));
    priv->status |= enable;

    return CMD_AT_RET_SUCCESS;
}
AT_CMD(USERWKMCUCFG, mcu_cmd_USERWKMCUCFG, AT_TYPE_ASSIGN);

static int mcu_cmd_USERMCUSLEEP(unsigned char type, unsigned char argc)
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();
    int enable, ret;

    MCU_COMMAND_CHECK_RETURN(MCU_CMD_WAKECFG_ISSET_ENABLE(priv->status) == 0, argc, "please AT+USERWKMCUCFG seting cfg");
    MCU_COMMAND_CHECK_RETURN(argc != AT_ARG_INDEX_1, argc, "AT+USERMCUSLEEP=<state>");
    ret = cmd_get_arg_digit_check(AT_ARG_INDEX_0, &enable, 0, 1);
    MCU_COMMAND_CHECK_RETURN(ret != CMD_RET_SUCCESS, AT_ARG_INDEX_0, "enable should be digit, range[0,1]");

    if (MCU_CMD_WAKELINK_ISSET_USERSLEEP(priv->wakelink)) {
        if (enable == 0) {
            // wake up
            MCU_CMD_WAKECFG_SET_UPSTATE(priv->status);
            if (g_mcu_wakeup_event_gp)
                xEventGroupSetBits(g_mcu_wakeup_event_gp, CMD_AT_USER_MCU_SLEEP);
        } else {
            MCU_CMD_WAKECFG_UNSET_UPSTATE(priv->status);
            if (g_mcu_wakeup_event_gp)
                xEventGroupClearBits(g_mcu_wakeup_event_gp, CMD_AT_USER_AWAKE);
            at_printf("\r\nOK\r\n");
            if (priv->wakemode == MCU_PSM_WAKEUP_GPIO) {
                drv_gpio_write(priv->wakenumber, !priv->wakesignal);
            }
            return CMD_AT_RET_UNHANDLED;
        }
    }

    return CMD_AT_RET_SUCCESS;
}
AT_CMD(USERMCUSLEEP, mcu_cmd_USERMCUSLEEP, AT_TYPE_ASSIGN);

int mcu_cmd_init(void)
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();
    g_mcu_wakeup_event_gp = xEventGroupCreate();
    MCU_COMMAND_CHECK_RETURN(g_mcu_wakeup_event_gp == NULL, -1, "mcu wakup event group create failed");
    MCU_CMD_WAKECFG_SET_UPSTATE(priv->status);

	return 0;
}

AT_INIT(mcucfg, mcu_cmd_init);

int mcu_cmd_cfg_dump(int argc, char *argv[])
{
    mcu_psmcfg_t *priv = mcu_command_get_priv();
    os_printf(LM_APP, LL_INFO, "#########################################\n");
    os_printf(LM_APP, LL_INFO, "status 0x%x linkstatus 0x%x delaytimems %d\n", priv->status, priv->wakelink, priv->delaytime);
    os_printf(LM_APP, LL_INFO, "wakemode %d wakenumber %d wakesignel %d\n", priv->wakemode, priv->wakenumber, priv->wakesignal);
    os_printf(LM_APP, LL_INFO, "#########################################\n");
    return CMD_RET_SUCCESS;
}

CLI_CMD(mcucfg, mcu_cmd_cfg_dump, "mcu_cmd_cfg_dump", "mcu_cmd_cfg_dump");
