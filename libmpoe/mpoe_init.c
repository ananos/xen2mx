#include "mpoe_lib.h"
#include "mpoe_internals.h"

struct mpoe_globals mpoe_globals = { 0 };

mpoe_return_t
mpoe__init_api(int api)
{
  if (mpoe_globals.initialized)
    return MPOE_ALREADY_INITIALIZED;

  mpoe_globals.initialized = 1;
  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  mpoe_globals.initialized = 0;
  return MPOE_SUCCESS;
}
