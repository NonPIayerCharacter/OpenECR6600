
MEMORY
{
	ilm_memory    (rwx) : org = 0x00010000, len = 192K
	dlm_memory    (!rx) : org = 0x00060000, len = 128K
	iram_memory   (rwx) : org = 0x00080000, len = 192K
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
	*libboard.a:(.text .text.*)
	*libpsm.a:(.text .text.*)
	*libarch.a:(.text .text.*)
	*libdrivers.a:(.text .text.*)
	*libos.a:(.text .text.*)
	*libps_wifi.a:(.text .text.*)
	*libps_ble.a:hci*.o(.text .text.*)
	*libps_ble.a:sch*.o(.text .text.*)
	*libps_ble.a:ble_thread.o(.text .text.*)
	*libps_ble.a:ecc_p256.o(.text .text.*)
	*(.ilm_text_drv)
	*(.ilm_text_wifi)
	*(.ilm_text_ble)
	*(.xip)
	
	*libps_ble.a:dbg*.o(.rodata .rodata.*)
	*libps_ble.a:hci*.o(.rodata .rodata.*)
	*libps_ble.a:llm*.o(.rodata .rodata.*)
	*libps_ble.a:lld*.o(.rodata .rodata.*)
	*libps_ble.a:ecc_p256.o(.rodata .rodata.*)
#ifdef CONFIG_OUTPUT_VERSION_STANDALONE_ELF
	*(.text .text.*)
#endif
#ifdef CONFIG_CPP
	KEEP(*(.fini ))
#endif
       	*(.ex9.itable )
       	. = ALIGN(0x4);
       	PROVIDE(_eilm = .);
    } > ilm_memory


#ifdef CONFIG_OUTPUT_VERSION_STANDALONE_ELF
  .data 0x60800 :
#else
  .data : AT(0x60800)
#endif
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
       	*(.rodata .rodata.* ) 
        KEEP(*(SORT(.cli_cmd* )))
	. = ALIGN(4);
	PROVIDE(_rodata = .);
       	*(.data*);
	. = ALIGN(0x4);
	*(.version.data);
       	PROVIDE(_edata = .);
       	. = ALIGN(0x4);
    }  > dlm_memory


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

#if !defined(CONFIG_OUTPUT_VERSION_STANDALONE_ELF)
  .text_iram :
  	{
    	*(.text .text.*)
	} > iram_memory
#endif

  SHARED (NOLOAD):
    {
        _sshram = . ;
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
        _eshram = . ;
    } > iram_memory

    _end = .;

    PROVIDE (_stack = 0xb0000);
    PROVIDE (buf_start_0 =  _eilm);
    PROVIDE (buf_start_1 =  _ebss); 
    PROVIDE (buf_start_2 =  _eshram); 
    PROVIDE (buf_len_0 =  0x40000 - buf_start_0); 
    PROVIDE (buf_len_1 =  0x7fac0 - buf_start_1); 
    PROVIDE (buf_len_2 =  LENGTH(iram_memory) - ( _eshram - _sshram)); 

	#include "lib_rom.h"
}
