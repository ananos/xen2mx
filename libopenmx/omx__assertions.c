#include "omx_io.h"
#include "omx__lib.h"
#include "omx__internals.h"

#include <net/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(sizeof(struct omx_evt_recv_tiny) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK(sizeof(union omx_evt) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_RECVQ_ENTRY_SIZE);
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_SENDQ_ENTRY_SIZE);
}
