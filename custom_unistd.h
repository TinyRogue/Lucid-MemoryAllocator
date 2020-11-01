#if !defined(_CUSTOM_UNISTD_H_)
#define _CUSTOM_UNISTD_H_

#include <unistd.h>
#include <assert.h>

void* custom_sbrk(intptr_t delta);

#if defined(sbrk)
#undef sbrk
#endif

#if defined(brk)
#undef brk
#endif


#define sbrk(__arg__) (assert("Proszę nie używać standardowej funkcji sbrk()" && 0), (void*)-1)
#define brk(__arg__) (assert("Proszę nie używać standardowej funkcji sbrk()" && 0), -1)

#endif // _CUSTOM_UNISTD_H_
