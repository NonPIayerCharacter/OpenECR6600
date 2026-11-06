#include "ak.h"

extern g_s_p_callback got_ssid_pwd_callback;

typedef enum
{
	AK_EVENT = 0,
	AK_EVENT_GOT_SSID_PSWD,
	AK_EVENT_MAX
} AK_EVENT_STATUS;

int ak_event_handler_register(short event, void* func, int reserver)
{
	switch(event)
	{
	case AK_EVENT_GOT_SSID_PSWD:
		got_ssid_pwd_callback = func;
		return 0;
	default:
		ak_error("event error!\n");
		return -1;
	}
}
