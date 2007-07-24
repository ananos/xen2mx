#ifndef __omx_internals_h__
#define __omx_internals_h__

#include "omx__lib.h"

#define OMX_DEVNAME "/dev/openmx"
/* FIXME: envvar to configure? */

#define OMX_MEDIUM_FRAG_PIPELINE_BASE 10  /* pipeline is encoded -10 on the wire */
#define OMX_MEDIUM_FRAG_PIPELINE 2 /* always send 4k pages (1<<(10+2)) */
#define OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT (OMX_MEDIUM_FRAG_PIPELINE_BASE+OMX_MEDIUM_FRAG_PIPELINE)
#define OMX_MEDIUM_FRAG_LENGTH_MAX (1<<OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)
#define OMX_MEDIUM_FRAGS_NR(len) ((len+OMX_MEDIUM_FRAG_LENGTH_MAX-1)>>OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)

extern omx_return_t omx__errno_to_return(int error, char * caller);

extern omx_return_t omx__get_board_count(uint32_t * count);
extern omx_return_t omx__get_board_id(struct omx_endpoint * ep, uint8_t * index, char * name, uint64_t * addr);
extern omx_return_t omx__get_board_index_by_name(const char * name, uint8_t * index);

struct omx_globals {
  int initialized;
  int control_fd;
  uint32_t board_max;
  uint32_t endpoint_max;
  uint32_t peer_max;
};

extern struct omx_globals omx_globals;

extern omx_return_t omx__peers_init(void);
extern omx_return_t omx__peers_dump(const char * format);

extern omx_return_t omx__peer_from_index(uint16_t index, uint64_t *board_addr, char **hostname);

#endif /* __omx_internals_h__ */
