#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "mpoe_lib.h"
#include "mpoe_internals.h"

struct mpoe_globals mpoe_globals = { 0 };

mpoe_return_t
mpoe__init_api(int api)
{
  int err;

  if (mpoe_globals.initialized)
    return MPOE_ALREADY_INITIALIZED;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0)
    return mpoe__errno_to_return(errno, "init open control fd");

  mpoe_globals.control_fd = err;

  mpoe_globals.initialized = 1;
  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  close(mpoe_globals.control_fd);

  mpoe_globals.initialized = 0;
  return MPOE_SUCCESS;
}
