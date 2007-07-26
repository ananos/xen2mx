#include "omx_types.h"
#include "omx_io.h"

#include <linux/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(sizeof(uint64_t) >= sizeof(((struct ethhdr *)NULL)->h_dest));
  CHECK(sizeof(uint64_t) >= sizeof(((struct ethhdr *)NULL)->h_source));
  CHECK(PAGE_SIZE%OMX_SENDQ_ENTRY_SIZE == 0);
  CHECK(PAGE_SIZE%OMX_RECVQ_ENTRY_SIZE == 0);
  CHECK(sizeof(union omx_evt) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK((unsigned) OMX_PKT_TYPE_MAX == (1<<(sizeof(((struct omx_pkt_msg*)NULL)->ptype)*8)) - 1);
}
