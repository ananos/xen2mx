#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "mpoe_lib.h"

int main(void)
{
  int fd, ret;
  uint32_t count;
  int i;

  fd = open(MPOE_DEVNAME, O_RDWR);
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
    struct mpoe_cmd_get_board_id board_id;
    char addr_str[MPOE_MAC_ADDR_STRLEN];

    /* get mac addr */
    board_id.board_index = i;
    ret = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &board_id);
    if (ret < 0) {
      perror("get board id");
      goto out_with_fd;
    }

    mpoe_mac_addr_sprintf(addr_str, &board_id.board_addr);
    fprintf(stderr, "board #%d name %s addr %s\n",
	    i, board_id.board_name, addr_str);
  }

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
