#ifndef __mpoe_internals_h__
#define __mpoe_internals_h__

#include "mpoe_lib.h"

#define MPOE_DEVNAME "/dev/mpoe"
/* FIXME: envvar to configure? */

#define MPOE_MEDIUM_FRAG_PIPELINE_BASE 10  /* pipeline is encoded -10 on the wire */
#define MPOE_MEDIUM_FRAG_PIPELINE 2 /* always send 4k pages (1<<(10+2)) */
#define MPOE_MEDIUM_FRAG_LENGTH_MAX_SHIFT (MPOE_MEDIUM_FRAG_PIPELINE_BASE+MPOE_MEDIUM_FRAG_PIPELINE)
#define MPOE_MEDIUM_FRAG_LENGTH_MAX (1<<MPOE_MEDIUM_FRAG_LENGTH_MAX_SHIFT)
#define MPOE_MEDIUM_FRAGS_NR(len) ((len+MPOE_MEDIUM_FRAG_LENGTH_MAX-1)>>MPOE_MEDIUM_FRAG_LENGTH_MAX_SHIFT)

extern mpoe_return_t mpoe__errno_to_return(int error, char * caller);

#endif /* __mpoe_internals_h__ */
