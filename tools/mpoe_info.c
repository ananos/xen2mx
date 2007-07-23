#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "mpoe_internals.h"

int main(void)
{
  mpoe_return_t ret;
  uint32_t max, emax, count;
  int found, i;

  ret = mpoe_init();
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            mpoe_strerror(ret));
    goto out;
  }

  /* get board and endpoint max */
  max = mpoe_globals.board_max;
  emax = mpoe_globals.endpoint_max;

  /* get board count */
  ret = mpoe__get_board_count(&count);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to read board count, %s\n", mpoe_strerror(ret));
    goto out;
  }
  printf("Found %ld boards (%ld max) supporting %ld endpoints each\n",
	 (unsigned long) count, (unsigned long) max, (unsigned long) emax);

  for(i=0, found=0; i<max && found<count; i++) {
    uint8_t board_index = i;
    char board_name[MPOE_HOSTNAMELEN_MAX];
    uint64_t board_addr;
    char board_addr_str[MPOE_BOARD_ADDR_STRLEN];

    ret = mpoe__get_board_id(NULL, &board_index, board_name, &board_addr);
    if (ret == MPOE_INVALID_PARAMETER)
      continue;
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to read board #%d id, %s\n", i, mpoe_strerror(ret));
      goto out;
    }

    assert(i == board_index);
    found++;

    mpoe_board_addr_sprintf(board_addr_str, board_addr);
    printf("board #%d name %s addr %s\n",
	   i, board_name, board_addr_str);
  }

  return 0;

 out:
  return -1;
}
