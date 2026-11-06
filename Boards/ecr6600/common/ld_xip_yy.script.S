
MEMORY
{
  ilm_memory	(rwx) : org = 0x00010000, len = 192K
  dlm_memory	(!rx) : org = 0x00060000, len = 0x1fa00
#if !defined(CONFIG_WIFI_PRODUCT_FHOST) && !defined(CONFIG_ESWIN_SDIO)
  iram_memory  	(rwx) : org = 0x000a0000, len = 64K
#else
  iram_memory  	(rwx) : org = 0x00080000, len = 190K

#endif
#if defined(XIP_ADDR_OFFSET)
  xip_memory  	(rx)  : org = 0x40800000+XIP_ADDR_OFFSET+0x100, len = 4M
#else
  xip_memory  	(rx)  : org = 0x40807100, len = 4M
#endif
}

ENTRY(_start)
SECTIONS
{
  .text : 
    {
	KEEP(*(.vector_init ));
#ifdef CONFIG_CPP
	KEEP(*(.init ))
#endif
    
	*libboard.a:chip_entry.o(.text .text.*)
	*libboard.a:chip_startup.o(.text .text.*)
	*libboard.a:chip_clk_ctrl.o(.text .text.*)
	//*libboard.a:(.text .text.*)
	TEXT_BEGIN_PM = .;
	*( .ilm_text_pm )
	//*libpsm.a:(.text .text.*)
	*libpsm.a:psm_mode_ctrl.o(.text .text.*)
	TEXT_SIZE_PM = . - TEXT_BEGIN_PM;

	TEXT_BEGIN_DRV = .;
	*( .ilm_text_drv )
	*libdrivers.a:dma.o(.text .text.*)
	*libdrivers.a:efuse.o(.text .text.*)
	*libdrivers.a:flash*.o(.text .text.*)

	*libdrivers.a:i2c.o(.text .text.*)
	*libdrivers.a:i2s.o(.text .text.*)
	*libdrivers.a:pit.o(.text .text.*)
	//*libdrivers.a:pwm.o(.text .text.*)
	*libdrivers.a:rtc.o(.text .text.*)

	*libdrivers.a:sdhci.o(.text .text.*)
	*libdrivers.a:spi_master.o(.text .text.*)
	*libdrivers.a:spi_slave_service.o(.text .text.*)
	*libdrivers.a:spi_slave.o(.text .text.*)

	//*libdrivers.a:timer.o(.text .text.*)
	//*libdrivers.a:trng.o(.text .text.*)
	//*libdrivers.a:uart.o(.text .text.*)
	*libdrivers.a:wdt.o(.text .text.*)

	*libdrivers.a:hal_system.o(.text .text.*)
	TEXT_SIZE_DRV = . - TEXT_BEGIN_DRV;

	TEXT_BEGIN_WIFI = .;
	*( .ilm_text_wifi )
	TEXT_SIZE_WIFI = . - TEXT_BEGIN_WIFI;

	TEXT_BEGIN_BLE = .;
	*( .ilm_text_ble )
	TEXT_SIZE_BLE = . - TEXT_BEGIN_BLE;

	*libarch.a:(.text .text.*)
	//*libdrivers.a:(.text .text.*)
	//*libos.a:(.text .text.*)
	*libos.a:irq.o(.text .text.*)
	*libos.a:list.o(.text .text.*)
	*libos.a:queue.o(.text .text.*)
	*libos.a:tasks.o(.text .text.*)
	*libos.a:timers.o(.text .text.*)
	*libos.a:port.o(.text .text.*)
	*libos.a:debug_core.o(.text .text.*)
	//*libps_wifi.a:hal_rf_6600.o(.text .text.*)

	//*libps_ble.a:sch_alarm.o(.text .text.*)
	//*libps_ble.a:sch_arb.o(.text .text.*)
	//*libps_ble.a:sch_plan.o(.text .text.*)
	//*libps_ble.a:sch_prog.o(.text .text.*)
	//*libps_ble.a:sch_slice.o(.text .text.*)
	//*libps_ble.a:lld_adv.o(.text .text.*)
	//*libps_ble.a:lld_con.o(.text .text.*)
	//*libps_ble.a:lld_scan.o(.text .text.*)
	//*libps_ble.a:lld_init.o(.text .text.*)
	//*libps_ble.a:lld.o(.text .text.*)
	//*libps_ble.a:rwip_driver.o(.text .text.*)
	//*libps_ble.a:rwble.o(.text .text.*)
	//*libps_ble.a:ble_util_buf.o(.text .text.*)
	//*libps_wifi.a:co_list_ble.o(.text .text.*)
	//*libps_wifi.a:ke.o(.text .text.*)
	//*libps_wifi.a:ke_event.o(.text .text.*)
	//*libps_wifi.a:ke_mem_freertos.o(.text .text.*)
	//*libps_wifi.a:ke_msg.o(.text .text.*)
	//*libps_wifi.a:ke_queue.o(.text .text.*)
	//*libps_wifi.a:ke_task.o(.text .text.*)
	//*libps_wifi.a:ke_timer.o(.text .text.*)
	//*libps_wifi.a:ke.o(.text .text.*)
	//*libps_wifi.a:ke_event.o(.text .text.*)
	//*libps_wifi.a:ke_mem_freertos.o(.text .text.*)
	//*libps_wifi.a:ke_msg.o(.text .text.*)
	//*libps_wifi.a:ke_queue.o(.text .text.*)
	//*libps_wifi.a:ke_task.o(.text .text.*)
	//*libps_wifi.a:ke_timer.o(.text .text.*)
#ifdef CONFIG_OPUS
	*libcomponents.a:opus_encoder.o(.text .text.*)
	*libcomponents.a:mathops.o(.text .text.*)
#endif
#ifdef CONFIG_CPP
	KEEP(*(.fini ))
#endif
	*(.ex9.itable )
	. = ALIGN(0x4);
	PROVIDE(_eilm = .);
    } > ilm_memory

  .bss :
    {
       . = ALIGN(4);
       PROVIDE(_sbss = .);
       *(.sbss_w .sbss_w.* )
       . = ALIGN(4);
       PROVIDE(__libandes_vsnprintf_countNow = .);
       *libandes.a:lib_a-vsnprintf.o(.bss.countNow)
       . = __libandes_vsnprintf_countNow + 4;
       PROVIDE(__libandes_vsnprintf_count = .);
       *libandes.a:lib_a-vsnprintf.o(.bss.count)
       . = __libandes_vsnprintf_count + 4;
       PROVIDE(__libandes_vprintf_output_idx = .);
       *libandes.a:lib_a-vprintf.o(.bss.output_idx)
       . = __libandes_vprintf_output_idx + 4;
       PROVIDE(__libandes_vprintf_output_buff = .);
       *libandes.a:lib_a-vprintf.o(.bss.output_buff)
       . = __libandes_vprintf_output_buff + 4;
       *(.bss*)
       *(COMMON)
       *(.scommon_*)
       . = ALIGN(4);
       PROVIDE(_ebss = .);
    } > dlm_memory

    SHARED (NOLOAD):
    {
        _sshram = . ;
        . += 0x44;
        *(LARAM)
#ifdef CONFIG_CPP
        PROVIDE(_sdma_data = .);
#endif
        *(.dma.data)
#ifdef CONFIG_CPP
        PROVIDE(_edma_data = .);
#endif
        *(SHAREDRAM)
        *(SHAREDRAMIPC)
#ifdef CONFIG_CPP
        PROVIDE(_sbss2 = .);
        PROVIDE(_ebss2 = .);
#endif
    } > iram_memory
	
	.data :
    {
       . = ALIGN(4);
       PROVIDE(_data_load = LOADADDR(.data));
       PROVIDE(_sdata = .);
#ifdef CONFIG_CPP
       //KEEP(*(.gcc_except_table )) 
       //*(.gcc_except_table.* )
       KEEP(*chip_cxx_crtbegin*.o(.ctors)) 
       KEEP(*(EXCLUDE_FILE (*chip_cxx_crtend*.o) .ctors)) 
       KEEP(*(SORT(.ctors.* ))) 
       KEEP(*(.ctors ))
       KEEP(*chip_cxx_crtbegin*.o(.dtors)) 
       KEEP(*(EXCLUDE_FILE (*chip_cxx_crtend*.o) .dtors)) 
       KEEP(*(SORT(.dtors.* ))) 
       KEEP(*(.dtors ))
#endif
       *(.sdata*);
	   . = ALIGN(4);
       *(.srodata*);
       . = ALIGN(4);
       *(.data*);
	   . = ALIGN(0x4);
       PROVIDE(_edata = .);
       . = ALIGN(0x4);
	   _eshram = . ;
    }  > iram_memory

	.xip :
    {
        *(.xip ) 
        *(.text .text.*)
        __etext1 = .;
        *(.rodata .rodata.* ) 
		*(.version.data);
        KEEP(*(SORT(.cli_cmd* )))
        KEEP(*(SORT(.at_cmd* )))
        KEEP(*(SORT(.at_init* )))
        . = ALIGN(4);
        __start_ads_local_audio = .;
        KEEP(*(ads_local_audio))
        __stop_ads_local_audio = .;
    } > xip_memory

    _end = .;

    PROVIDE (_stack = 0xb0000);  /* main stack */
    PROVIDE (buf_start_0 =  _eilm);
    PROVIDE (buf_start_1 =  _ebss); 
    PROVIDE (buf_start_2 =  _eshram); 
    PROVIDE (buf_len_0 =  0x40000 - buf_start_0); 
    PROVIDE (buf_len_1 =  0x7fac0 - buf_start_1);  /* jump bootrom rodata */
    PROVIDE (buf_len_2 =  LENGTH(iram_memory) - ( _eshram - _sshram));

    #include "lib_rom.h"
}
