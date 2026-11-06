
#include <stdbool.h>
#include <stdint.h>
#include "oshal.h"


#define _ATOMIC_ENTER_CRITICAL() ({ \
    unsigned state = system_irq_save(); \
    state; \
})

#define _ATOMIC_EXIT_CRITICAL(state)   do { \
    system_irq_restore(state); \
    } while (0)




#ifdef __clang__
// Clang doesn't allow to define "__sync_*" atomics. The workaround is to define function with name "__sync_*_builtin",
// which implements "__sync_*" atomic functionality and use asm directive to set the value of symbol "__sync_*" to the name
// of defined function.

#define CLANG_ATOMIC_SUFFIX(name_) name_ ## _builtin
#define CLANG_DECLARE_ALIAS(name_) \
__asm__(".type " # name_ ", @function\n"        \
        ".global " #name_ "\n"                  \
        ".equ " #name_ ", " #name_ "_builtin");

#else // __clang__

#define CLANG_ATOMIC_SUFFIX(name_) name_
#define CLANG_DECLARE_ALIAS(name_)

#endif // __clang__



#define ATOMIC_LOAD(n, type) type __atomic_load_ ## n (const type* mem, int memorder) \
{                                                   \
    unsigned state = _ATOMIC_ENTER_CRITICAL();      \
    type ret = *mem;                                \
    _ATOMIC_EXIT_CRITICAL(state);                   \
    return ret;                                     \
}

#define ATOMIC_STORE(n, type) void __atomic_store_ ## n (type* mem, type val, int memorder) \
{                                                   \
    unsigned state = _ATOMIC_ENTER_CRITICAL();      \
    *mem = val;                                     \
    _ATOMIC_EXIT_CRITICAL(state);                   \
}

#define ATOMIC_EXCHANGE(n, type) type __atomic_exchange_ ## n (type* mem, type val, int memorder) \
{                                                   \
    unsigned state = _ATOMIC_ENTER_CRITICAL();      \
    type ret = *mem;                                \
    *mem = val;                                     \
    _ATOMIC_EXIT_CRITICAL(state);                   \
    return ret;                                     \
}

#define CMP_EXCHANGE(n, type) bool __atomic_compare_exchange_ ## n (type* mem, type* expect, type desired, bool weak, int success, int failure) \
{ \
    bool ret = false; \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    if (*mem == *expect) { \
        ret = true; \
        *mem = desired; \
    } else { \
        *expect = *mem; \
    } \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_ADD(n, type) type __atomic_fetch_add_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = *ptr + value; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define ADD_FETCH(n, type) type __atomic_add_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr + value; \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_SUB(n, type) type __atomic_fetch_sub_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = *ptr - value; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define SUB_FETCH(n, type) type __atomic_sub_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr - value; \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_AND(n, type) type __atomic_fetch_and_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = *ptr & value; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define AND_FETCH(n, type) type __atomic_and_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr & value; \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_OR(n, type) type __atomic_fetch_or_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = *ptr | value; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define OR_FETCH(n, type) type __atomic_or_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr | value; \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_XOR(n, type) type __atomic_fetch_xor_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = *ptr ^ value; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define XOR_FETCH(n, type) type __atomic_xor_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr ^ value; \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define FETCH_NAND(n, type) type __atomic_fetch_nand_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = *ptr; \
    *ptr = ~(*ptr & value); \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define NAND_FETCH(n, type) type __atomic_nand_fetch_ ## n (type* ptr, type value, int memorder) \
{ \
    unsigned state = _ATOMIC_ENTER_CRITICAL(); \
    type ret = ~(*ptr & value); \
    *ptr = ret; \
    _ATOMIC_EXIT_CRITICAL(state); \
    return ret; \
}

#define SYNC_FETCH_OP(op, n, type) type CLANG_ATOMIC_SUFFIX(__sync_fetch_and_ ## op ##_ ## n) (type* ptr, type value) \
{                                                                                \
    return __atomic_fetch_ ## op ##_ ## n (ptr, value, __ATOMIC_SEQ_CST);        \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_fetch_and_ ## op ##_ ## n )

#define SYNC_OP_FETCH(op, n, type) type CLANG_ATOMIC_SUFFIX(__sync_ ## op ##_and_fetch_ ## n) (type* ptr, type value) \
{                                                                                \
    return __atomic_ ## op ##_fetch_ ## n (ptr, value, __ATOMIC_SEQ_CST);        \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_ ## op ##_and_fetch_ ## n )

#define SYNC_BOOL_CMP_EXCHANGE(n, type) bool  CLANG_ATOMIC_SUFFIX(__sync_bool_compare_and_swap_ ## n)  (type *ptr, type oldval, type newval) \
{                                                                                \
    bool ret = false;                                                            \
    unsigned state = _ATOMIC_ENTER_CRITICAL();                                   \
    if (*ptr == oldval) {                                                        \
        *ptr = newval;                                                           \
        ret = true;                                                              \
    }                                                                            \
    _ATOMIC_EXIT_CRITICAL(state);                                                \
    return ret;                                                                  \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_bool_compare_and_swap_ ## n )

#define SYNC_VAL_CMP_EXCHANGE(n, type) type  CLANG_ATOMIC_SUFFIX(__sync_val_compare_and_swap_ ## n)  (type *ptr, type oldval, type newval) \
{                                                                                \
    unsigned state = _ATOMIC_ENTER_CRITICAL();                                   \
    type ret = *ptr;                                                             \
    if (*ptr == oldval) {                                                        \
        *ptr = newval;                                                           \
    }                                                                            \
    _ATOMIC_EXIT_CRITICAL(state);                                                \
    return ret;                                                                  \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_val_compare_and_swap_ ## n )

#define SYNC_LOCK_TEST_AND_SET(n, type) type  CLANG_ATOMIC_SUFFIX(__sync_lock_test_and_set_ ## n)  (type *ptr, type val) \
{                                                                                \
    unsigned state = _ATOMIC_ENTER_CRITICAL();                                   \
    type ret = *ptr;                                                             \
    *ptr = val;                                                                  \
    _ATOMIC_EXIT_CRITICAL(state);                                                \
    return ret;                                                                  \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_lock_test_and_set_ ## n )

#define SYNC_LOCK_RELEASE(n, type) void  CLANG_ATOMIC_SUFFIX(__sync_lock_release_ ## n)  (type *ptr) \
{                                                                                \
    unsigned state = _ATOMIC_ENTER_CRITICAL();                                   \
    *ptr = 0;                                                                    \
    _ATOMIC_EXIT_CRITICAL(state);                                                \
}                                                                                \
CLANG_DECLARE_ALIAS( __sync_lock_release_ ## n )



ATOMIC_EXCHANGE(1, uint8_t)
ATOMIC_EXCHANGE(2, uint16_t)
ATOMIC_EXCHANGE(4, uint32_t)

CMP_EXCHANGE(1, uint8_t)
CMP_EXCHANGE(2, uint16_t)
CMP_EXCHANGE(4, uint32_t)

FETCH_ADD(1, uint8_t)
FETCH_ADD(2, uint16_t)
FETCH_ADD(4, uint32_t)

ADD_FETCH(1, uint8_t)
ADD_FETCH(2, uint16_t)
ADD_FETCH(4, uint32_t)

FETCH_SUB(1, uint8_t)
FETCH_SUB(2, uint16_t)
FETCH_SUB(4, uint32_t)

SUB_FETCH(1, uint8_t)
SUB_FETCH(2, uint16_t)
SUB_FETCH(4, uint32_t)

FETCH_AND(1, uint8_t)
FETCH_AND(2, uint16_t)
FETCH_AND(4, uint32_t)

AND_FETCH(1, uint8_t)
AND_FETCH(2, uint16_t)
AND_FETCH(4, uint32_t)

FETCH_OR(1, uint8_t)
FETCH_OR(2, uint16_t)
FETCH_OR(4, uint32_t)

OR_FETCH(1, uint8_t)
OR_FETCH(2, uint16_t)
OR_FETCH(4, uint32_t)

FETCH_XOR(1, uint8_t)
FETCH_XOR(2, uint16_t)
FETCH_XOR(4, uint32_t)

XOR_FETCH(1, uint8_t)
XOR_FETCH(2, uint16_t)
XOR_FETCH(4, uint32_t)

FETCH_NAND(1, uint8_t)
FETCH_NAND(2, uint16_t)
FETCH_NAND(4, uint32_t)

NAND_FETCH(1, uint8_t)
NAND_FETCH(2, uint16_t)
NAND_FETCH(4, uint32_t)

SYNC_FETCH_OP(add, 1, uint8_t)
SYNC_FETCH_OP(add, 2, uint16_t)
SYNC_FETCH_OP(add, 4, uint32_t)

SYNC_OP_FETCH(add, 1, uint8_t)
SYNC_OP_FETCH(add, 2, uint16_t)
SYNC_OP_FETCH(add, 4, uint32_t)

SYNC_FETCH_OP(sub, 1, uint8_t)
SYNC_FETCH_OP(sub, 2, uint16_t)
SYNC_FETCH_OP(sub, 4, uint32_t)

SYNC_OP_FETCH(sub, 1, uint8_t)
SYNC_OP_FETCH(sub, 2, uint16_t)
SYNC_OP_FETCH(sub, 4, uint32_t)

SYNC_FETCH_OP(and, 1, uint8_t)
SYNC_FETCH_OP(and, 2, uint16_t)
SYNC_FETCH_OP(and, 4, uint32_t)

SYNC_OP_FETCH(and, 1, uint8_t)
SYNC_OP_FETCH(and, 2, uint16_t)
SYNC_OP_FETCH(and, 4, uint32_t)

SYNC_FETCH_OP(or, 1, uint8_t)
SYNC_FETCH_OP(or, 2, uint16_t)
SYNC_FETCH_OP(or, 4, uint32_t)

SYNC_OP_FETCH(or, 1, uint8_t)
SYNC_OP_FETCH(or, 2, uint16_t)
SYNC_OP_FETCH(or, 4, uint32_t)

SYNC_FETCH_OP(xor, 1, uint8_t)
SYNC_FETCH_OP(xor, 2, uint16_t)
SYNC_FETCH_OP(xor, 4, uint32_t)

SYNC_OP_FETCH(xor, 1, uint8_t)
SYNC_OP_FETCH(xor, 2, uint16_t)
SYNC_OP_FETCH(xor, 4, uint32_t)

SYNC_FETCH_OP(nand, 1, uint8_t)
SYNC_FETCH_OP(nand, 2, uint16_t)
SYNC_FETCH_OP(nand, 4, uint32_t)

SYNC_OP_FETCH(nand, 1, uint8_t)
SYNC_OP_FETCH(nand, 2, uint16_t)
SYNC_OP_FETCH(nand, 4, uint32_t)

SYNC_BOOL_CMP_EXCHANGE(1, uint8_t)
SYNC_BOOL_CMP_EXCHANGE(2, uint16_t)
SYNC_BOOL_CMP_EXCHANGE(4, uint32_t)

SYNC_VAL_CMP_EXCHANGE(1, uint8_t)
SYNC_VAL_CMP_EXCHANGE(2, uint16_t)
SYNC_VAL_CMP_EXCHANGE(4, uint32_t)


SYNC_LOCK_TEST_AND_SET(1, uint8_t)
SYNC_LOCK_TEST_AND_SET(2, uint16_t)
SYNC_LOCK_TEST_AND_SET(4, uint32_t)

SYNC_LOCK_RELEASE(1, uint8_t)
SYNC_LOCK_RELEASE(2, uint16_t)
SYNC_LOCK_RELEASE(4, uint32_t)

// LLVM has not implemented native atomic load/stores for riscv targets without the Atomic extension. 
// LLVM thread: https://reviews.llvm.org/D47553.
// Even though GCC does transform them, these libcalls need to be available for the case where a LLVM based project links against IDF.
ATOMIC_LOAD(1, uint8_t)
ATOMIC_LOAD(2, uint16_t)
ATOMIC_LOAD(4, uint32_t)
ATOMIC_STORE(1, uint8_t)
ATOMIC_STORE(2, uint16_t)
ATOMIC_STORE(4, uint32_t)

