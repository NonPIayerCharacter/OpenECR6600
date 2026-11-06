#ifndef HAL_TRNG_H
#define HAL_TRNG_H

#include "trng.h"

#ifdef __cplusplus
extern "C"
{
#endif

unsigned int hal_trng_get(void);
unsigned int hal_prng_get(void);

#ifdef __cplusplus
}
#endif

#endif /*HAL_TRNG_H*/


