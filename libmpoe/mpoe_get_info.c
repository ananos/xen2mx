#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "mpoe_io.h"
#include "mpoe_lib.h"
#include "mpoe_internals.h"

/*
 * Returns the max amount of boards supported by the driver
 */
mpoe_return_t
mpoe__get_board_max(uint32_t * max)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err, fd;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "open");
    goto out;
  }
  fd = err;

  err = ioctl(fd, MPOE_CMD_GET_BOARD_MAX, max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out_with_fd;
  }

 out_with_fd:
  close(fd);
 out:
  return ret;
}

/*
 * Returns the max amount of endpoints per board supported by the driver
 */
mpoe_return_t
mpoe__get_endpoint_max(uint32_t * max)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err, fd;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "open");
    goto out;
  }
  fd = err;

  err = ioctl(fd, MPOE_CMD_GET_ENDPOINT_MAX, max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_ENDPOINT_MAX");
    goto out_with_fd;
  }

 out_with_fd:
  close(fd);
 out:
  return ret;
}

/*
 * Returns the current amount of boards attached to the driver
 */
mpoe_return_t
mpoe__get_board_count(uint32_t * count)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err, fd;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "open");
    goto out;
  }
  fd = err;

  err = ioctl(fd, MPOE_CMD_GET_BOARD_COUNT, count);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_COUNT");
    goto out_with_fd;
  }

 out_with_fd:
  close(fd);
 out:
  return ret;
}

/*
 * Returns the board id of the endpoint is non NULL,
 * or the current board corresponding to the index.
 *
 * index, name and addr pointers may be NULL is unused.
 */
mpoe_return_t
mpoe__get_board_id(struct mpoe_endpoint * ep, uint8_t * index,
		   char * name, struct mpoe_mac_addr * addr)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  struct mpoe_cmd_get_board_id board_id;
  int err, fd;

  if (ep) {
    /* use the endpoint */
    fd = ep->fd;
  } else {
    /* use a dummy endpoint and the index */
    err = open(MPOE_DEVNAME, O_RDONLY);
    if (err < 0) {
      ret = mpoe__errno_to_return(errno, "open");
      goto out;
    }
    fd = err;

    board_id.board_index = *index;
  }

  err = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &board_id);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_ID");
    goto out_with_fd;
  }

  if (name)
    strncpy(name, board_id.board_name, MPOE_IF_NAMESIZE);
  if (index)
    *index = board_id.board_index;
  if (addr)
    mpoe_mac_addr_copy(addr, &board_id.board_addr);

 out_with_fd:
  if (!ep)
    close(fd);
 out:
  return ret;
}

/*
 * Returns the current index of a board given by its name
 */
mpoe_return_t
mpoe__get_board_index_by_name(const char * name, uint8_t * index)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  uint32_t max;
  int err, fd, i;

  err = open(MPOE_DEVNAME, O_RDONLY);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "open");
    goto out;
  }
  fd = err;

  err = ioctl(fd, MPOE_CMD_GET_BOARD_MAX, &max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out_with_fd;
  }

  ret = MPOE_INVALID_PARAMETER;
  for(i=0; i<max; i++) {
    struct mpoe_cmd_get_board_id board_id;

    board_id.board_index = i;
    err = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &board_id);
    if (err < 0) {
      ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_ID");
      if (ret != MPOE_INVALID_PARAMETER)
	goto out_with_fd;
    }

    if (!strncmp(name, board_id.board_name, MPOE_IF_NAMESIZE)) {
      ret = MPOE_SUCCESS;
      *index = i;
      break;
    }
  }

 out_with_fd:
  close(fd);
 out:
  return ret;
}

/* returns various info */
mpoe_return_t
mpoe_get_info(struct mpoe_endpoint * ep, enum mpoe_info_key key,
	      const void * in_val, uint32_t in_len,
	      void * out_val, uint32_t out_len)
{
  switch (key) {
  case MPOE_INFO_BOARD_MAX:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_board_max((uint32_t *) out_val);

  case MPOE_INFO_ENDPOINT_MAX:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_endpoint_max((uint32_t *) out_val);

  case MPOE_INFO_BOARD_COUNT:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_board_count((uint32_t *) out_val);

  case MPOE_INFO_BOARD_INDEX:
    /* FIXME: by endpoint, name or mac addr */
  case MPOE_INFO_BOARD_NAME:
    /* FIXME: by endpoint or index */
  case MPOE_INFO_BOARD_ADDR:
    /* FIXME: by endpoint or index */
    return MPOE_NOT_IMPLEMENTED;

  default:
    return MPOE_INVALID_PARAMETER;
  }

  return MPOE_SUCCESS;
}
