#include "mpoe_lib.h"

int mpoe_initialized = 0;

mpoe_return_t
mpoe__init_api(int api)
{
  if (mpoe_initialized)
    return MPOE_ALREADY_INITIALIZED;

  mpoe_initialized = 1;
  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  mpoe_initialized = 0;
  return MPOE_SUCCESS;
}
