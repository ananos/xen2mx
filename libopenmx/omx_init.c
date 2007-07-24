#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "omx__lib.h"
#include "omx__internals.h"

struct mpoe_globals mpoe_globals = { 0 };

mpoe_return_t
mpoe__init_api(int api)
{
  mpoe_return_t ret;
  int err;

  if (mpoe_globals.initialized)
    return MPOE_ALREADY_INITIALIZED;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0)
    return mpoe__errno_to_return(errno, "init open control fd");

  mpoe_globals.control_fd = err;

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_MAX, &mpoe_globals.board_max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out_with_fd;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_ENDPOINT_MAX, &mpoe_globals.endpoint_max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_ENDPOINT_MAX");
    goto out_with_fd;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_PEER_MAX, &mpoe_globals.peer_max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_PEER_MAX");
    goto out_with_fd;
  }

  mpoe_globals.initialized = 1;
  return MPOE_SUCCESS;

 out_with_fd:
  close(mpoe_globals.control_fd);
  return ret;
}

mpoe_return_t
mpoe_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  close(mpoe_globals.control_fd);

  mpoe_globals.initialized = 0;
  return MPOE_SUCCESS;
}
