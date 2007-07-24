#include "omx_types.h"
#include "mpoe_io.h"

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
  CHECK(PAGE_SIZE%MPOE_SENDQ_ENTRY_SIZE == 0);
  CHECK(PAGE_SIZE%MPOE_RECVQ_ENTRY_SIZE == 0);
  CHECK(sizeof(union mpoe_evt) == MPOE_EVENTQ_ENTRY_SIZE);
  CHECK((unsigned) MPOE_PKT_TYPE_MAX == (1<<(sizeof(((struct mpoe_pkt_msg*)NULL)->ptype)*8)) - 1);
}
