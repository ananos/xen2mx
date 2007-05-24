#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "mpoe_io.h"

#define DEVNAME "/dev/mpoe"

int main(void)
{
  int fd, ret;
  uint32_t count;
  int i;
  struct mpoe_cmd_get_board_id get_board_id;

  fd = open(DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  /* get board count */
  ret = ioctl(fd, MPOE_CMD_GET_BOARD_COUNT, &count);
  if (ret < 0) {
    perror("get board count");
    goto out_with_fd;
  }

  for(i=0; i<count; i++) {
    /* get mac addr */
    get_board_id.board_index = i;
    ret = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &get_board_id);
    if (ret < 0) {
      perror("get board id");
      goto out_with_fd;
    }
    fprintf(stderr, "board #%d name %s addr %02x:%02x:%02x:%02x:%02x:%02x\n",
	    i,
	    get_board_id.board_name,
	    (uint8_t) (get_board_id.board_addr >> 40),
	    (uint8_t) (get_board_id.board_addr >> 32),
	    (uint8_t) (get_board_id.board_addr >> 24),
	    (uint8_t) (get_board_id.board_addr >> 16),
	    (uint8_t) (get_board_id.board_addr >> 8),
	    (uint8_t) get_board_id.board_addr);
  }

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
