#include "mpoe_io.h"
#include "mpoe_lib.h"
#include "mpoe_internals.h"

#include <net/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(sizeof(struct mpoe_evt_recv_tiny) == MPOE_EVENTQ_ENTRY_SIZE);
  CHECK(sizeof(union mpoe_evt) == MPOE_EVENTQ_ENTRY_SIZE);
  CHECK(MPOE_IF_NAMESIZE == IF_NAMESIZE);
}
