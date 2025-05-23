/* wangc add ram link flie */


MEMORY
{
  ilm_memory (rx) : org = 0x00010000, len = 192K
  dlm_memory (!rx): org = 0x00060000, len = 128K
  iram0_memory  (rx) : org = 0x00080000, len = 128K
  iram1_memory  (rx) : org = 0x000a0000, len = 64K
}

PHDRS
{
   text PT_LOAD ;
   xip	PT_LOAD ;
   data PT_LOAD ;
   bss  PT_LOAD ;
};

ENTRY(_start)
SECTIONS
{
	/* shared RAM */
    SHARED0 (NOLOAD):
    {
        _sshram0 = . ;
        *(LARAM)
        _eshram0 = . ;
        _trace_start = .;
        _trace_end = ORIGIN(iram0_memory) + LENGTH(iram0_memory);
    } > iram0_memory

  .text : 
    {
		KEEP(*(.vector_init ));
       *(.text .text.* .libgcc);
       *(.ex9.itable )
/*
       build/libs/libapps.a:*.o(.text .text.*)
       build/libs/libcomponents.a:*.o(.text .text.*)
*/
       . = ALIGN(0x4);
    } > ilm_memory :text

  .rodata :
    {
       *(.rodata*);
       . = ALIGN(4);
       KEEP(*(SORT(.cli_cmd* )))
       . = ALIGN(4);
	   PROVIDE(_rodata = .);
    } > ilm_memory :text

  .data :
    {
       PROVIDE(_data_load = LOADADDR(.data));
       PROVIDE(_sdata = .);
       *(.sdata*);
	   . = ALIGN(4);
       *(.srodata*);
       . = ALIGN(4);
       *(.data*);
       PROVIDE(_edata = .);
       . = ALIGN(0x4);
    }  > dlm_memory :data

  .bss :
    {
       . = ALIGN(4);
       PROVIDE(_sbss = .);
       *(.sbss_w .sbss_w.* )
       *(.bss*)
       *(COMMON)
       *(.scommon_*)
       . = ALIGN(4);
       PROVIDE(_ebss = .);
    } > dlm_memory AT > dlm_memory :bss

    SHARED (NOLOAD):
    {
        _sshram = . ;
        *(SHAREDRAM)
        *(SHAREDRAMIPC)
        _eshram = . ;
    } > iram1_memory

    _end = .;

    PROVIDE (_stack = 0x7fa00);
    PROVIDE (buf_start_0 =  _rodata);
    PROVIDE (buf_start_1 =  _ebss); 
    /*PROVIDE (buf_start_2 =  _e_iram0);*/ 
    PROVIDE (buf_len_0 =  0x40000 - _rodata); 
    PROVIDE (buf_len_1 =  0x7fac0 - buf_start_1); 
    /*PROVIDE (buf_len_2 =  LENGTH(iram0_memory) - ( _e_iram0 - _s_iram0)); */
}
