


typedef void (*func_ptr) (void);


func_ptr __CTOR_END__[1] __attribute__((__unused__)) __attribute__ ((section (".ctors")))
     = { (func_ptr) 0 };

func_ptr __DTOR_END__[1] __attribute__((__unused__)) __attribute__ ((section (".dtors")))
     = { (func_ptr) 0 };


void __do_global_dtors (void)
asm ("__do_global_dtors") __attribute__ ((section (".text")));

void __do_global_ctors (void)
{
	func_ptr *p;
	for (p = __CTOR_END__ - 1; *p != (func_ptr) -1; p--)
		(*p) ();
}


asm ("\n\
	.section .init\n\
	j	__do_global_ctors\n\
");


asm ("\n\
	.section .fini\n\
");
