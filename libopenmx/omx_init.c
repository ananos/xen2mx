#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "omx__lib.h"

struct omx_globals omx_globals = { 0 };

omx_return_t
omx__init_api(int api)
{
  omx_return_t ret;
  int err;

  if (omx_globals.initialized)
    return OMX_ALREADY_INITIALIZED;

  err = open(OMX_DEVNAME, O_RDONLY);
  if (err < 0)
    return omx__errno_to_return(errno, "init open control fd");

  omx_globals.control_fd = err;

  err = ioctl(omx_globals.control_fd, OMX_CMD_GET_BOARD_MAX, &omx_globals.board_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out_with_fd;
  }

  err = ioctl(omx_globals.control_fd, OMX_CMD_GET_ENDPOINT_MAX, &omx_globals.endpoint_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_ENDPOINT_MAX");
    goto out_with_fd;
  }

  err = ioctl(omx_globals.control_fd, OMX_CMD_GET_PEER_MAX, &omx_globals.peer_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_PEER_MAX");
    goto out_with_fd;
  }

  ret = omx__peers_init();
  if (ret != OMX_SUCCESS)
    goto out_with_fd;

  omx_globals.initialized = 1;
  return OMX_SUCCESS;

 out_with_fd:
  close(omx_globals.control_fd);
  return ret;
}

omx_return_t
omx_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  close(omx_globals.control_fd);

  omx_globals.initialized = 0;
  return OMX_SUCCESS;
}
