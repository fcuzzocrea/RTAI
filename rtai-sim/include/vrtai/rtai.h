#ifndef _RTAI_RTAI_H_
#define _RTAI_RTAI_H_

#if defined(__MVM__) || defined(__MVMSA__)
#include "vrtai.h"
#else /* !(__MVM__ && __MVMSA__) */
#include <asm/rtai.h>
#endif /* __MVM__ || __MVMSA__ */

#endif
