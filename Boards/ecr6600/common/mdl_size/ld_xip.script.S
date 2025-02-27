/* used to cacl module size, cannot run */


MEMORY
{
   v_memory (rwx) : org = 0x50010000, len = 8192K
}

ENTRY(_start)
SECTIONS
{
    #include "mdl.ld" 
    
  .text : 
    {
	KEEP(*(.vector_init ));
        *(.text .text.*)	
	*(.ex9.itable )
    } > v_memory

  .bss :
    {
       . = ALIGN(4);
       *(.sbss_w .sbss_w.* .bss* COMMON .scommon_*)
    } > v_memory

    SHARED (NOLOAD):
    {
        _sshram = . ;
        *(LARAM)
        *(.dma.data)
        *(SHAREDRAM)
        *(SHAREDRAMIPC)
    } > v_memory
	
   .data :
    {
       . = ALIGN(4);
       *(.sdata* .srodata* .data*);
    }  > v_memory

   .rodata :
    {
        *(.xip ) 
        *(.rodata .rodata.*) 
	*(.version.data);
    } > v_memory

   .xip :
    {
        *(.xip ) 
    } > v_memory

    _end = .;

    PROVIDE(__etext1 = 0);
    PROVIDE(_sdata = 0);
    PROVIDE(_eshram = 0);
    PROVIDE(_data_load = 0);
    PROVIDE(_edata = 0);
    PROVIDE(_sbss = 0);
    PROVIDE(_ebss = 0);    
    PROVIDE (_stack = 0xb0000);  /* main stack */
    PROVIDE (buf_start_0 =  0);
    PROVIDE (buf_start_1 =  0); 
    PROVIDE (buf_start_2 =  0); 
    PROVIDE (buf_len_0 =  0x40000); 
    PROVIDE (buf_len_1 =  0x7fac0);  /* jump bootrom rodata */
    PROVIDE (buf_len_2 =  0x7fac0);

    #include "lib_rom.h"
}
