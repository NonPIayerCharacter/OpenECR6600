


typedef void (*func_ptr) (void);

func_ptr __CTOR_LIST__[1] __attribute__((__unused__)) __attribute__ ((section (".ctors")))
     = { (func_ptr) (-1) };

func_ptr __DTOR_LIST__[1] __attribute__((__unused__)) __attribute__ ((section (".dtors")))
     = { (func_ptr) (-1) };



void __do_global_dtors (void)
asm ("__do_global_dtors") __attribute__ ((section (".text")));

void __do_global_dtors (void)
{
	func_ptr *p;
	for (p = __DTOR_LIST__ + 1; *p; p++)
		(*p) ();
}


asm ("\n\
	.section .init\n\
	.global _crt_init\n\
	.type	_crt_init, @function\n\
	.align 2\n\
_crt_init:\n\
");



asm ("\n\
	.section .fini\n\
	.global _crt_fini\n\
	.type	_crt_fini, @function\n\
	.align 2\n\
_crt_fini:\n\
	j	__do_global_dtors\n\
");
