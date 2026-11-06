
#include <stdlib.h>
#include <string.h>
#include "chip_clk_ctrl.h"
#include "arch_cpu.h"
#include "pit.h"
#include "efuse.h"
#include "flash.h"
#include "aes.h"
#include "oshal.h"
#include "encrypt_lock.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "rtc.h"
#include "hal_system.h"
#include "hal_rtc.h"
#include "chip_configuration.h"
#include "psm_system.h"

#ifdef CONFIG_CPP
extern void _crt_init(void);
extern void _crt_fini(void);

/*
 * When compiling C++ code with static objects, the compiler inserts
 * a call to __cxa_atexit() with __dso_handle as one of the arguments.
 * The dummy versions of these symbols should be provided.
 */
void __cxa_atexit(void (*arg1)(void*), void* arg2, void* arg3)
{
}

void* __dso_handle = (void*) &__dso_handle;
#endif

void chip_c_init(void)
{
	extern unsigned int _sbss, _ebss;

	unsigned int *dest;

	extern unsigned char _sdata, _edata, _data_load;
	unsigned int size = &_edata - &_sdata;

	/* Copy data section from LMA to VMA */
	if(&_sdata != &_data_load)
	{
		memcpy(&_sdata, &_data_load, size);
	}

	/* clear .bss */
	for (dest = &_sbss; dest < &_ebss; )
	{
	    *dest++ = 0;
	}

#ifdef CONFIG_CPP
	extern unsigned int _sbss2, _ebss2;
	extern unsigned int _sdma_data, _edma_data;

	/* clear .bss2 */
	for (dest = &_sbss2; dest < &_ebss2; )
	{
	    *dest++ = 0;
	}

    /* clear .dma.data */
	for (dest = &_sdma_data; dest < &_edma_data; )
	{
	    *dest++ = 0;
	}
#endif
}



void chip_startup(void)
{
extern int main(void);
	chip_c_init();
	arch_cpu_init(); 
	drv_pit_init();

#if defined(CONFIG_EFUSE)
		drv_efuse_init();
#endif //CONFIG_EFUSE

	chip_clk_init();

	vPortHeapInit();

	/* To do bufore hal_rtc_init 
	 * It is determined via the RTC enable register that if reset key restart*/
	hal_reset_type_init(); 
	
#if defined(CONFIG_RTC)
	//drv_rtc_init();
	if (hal_get_reset_type() != RST_TYPE_SOFTWARE_REBOOT)
	{
	hal_rtc_init();
	}
	else
	{
		hal_rtc_restore();
	}
#endif //CONFIG_RTC

#ifdef CONFIG_AES
	drv_aes_init();
#endif //CONFIG_AES

#ifdef CONFIG_ENCRYPT_LOCK
	drv_encrypt_lock_init();
#endif //

#if defined(CONFIG_GPIO)
	drv_gpio_init();
#endif //CONFIG_GPIO
	
#ifdef CONFIG_FLASH
	drv_spiFlash_init();
#endif //CONFIG_FLASH

#ifdef CONFIG_CPU_DCACHE_ENABLE
	arch_dcache_enable(ARCH_DCACHE_ENABLE);
#endif

#ifdef CONFIG_CPU_ICACHE_ENABLE
	arch_icache_enable(ARCH_ICACHE_ENABLE);
#endif

    init_ate_efuse_info();
	
#ifdef CONFIG_SYSTEM_IRQ
	sys_irq_sw_set();
#endif

#ifdef CONFIG_PSM_SURPORT
	psm_pad_gpio_status_init();
#endif

#ifdef CONFIG_AI_VLLM
extern void ledc_deinit();
	ledc_deinit();
#endif

#ifdef CONFIG_CPP
	/* Do global constructors */
	_crt_init();

	/* Register global destructors to be called upon exit */
	atexit(_crt_fini);
#endif

	main();
}
