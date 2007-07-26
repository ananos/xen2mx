#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "omx_lib.h"

int main(void)
{
  omx_return_t ret;
  uint32_t max, emax, count;
  int found, i;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  /* get board and endpoint max */
  max = omx__globals.board_max;
  emax = omx__globals.endpoint_max;

  /* get board count */
  ret = omx__get_board_count(&count);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board count, %s\n", omx_strerror(ret));
    goto out;
  }
  printf("Found %ld boards (%ld max) supporting %ld endpoints each\n",
	 (unsigned long) count, (unsigned long) max, (unsigned long) emax);

  for(i=0, found=0; i<max && found<count; i++) {
    uint8_t board_index = i;
    char board_name[OMX_HOSTNAMELEN_MAX];
    uint64_t board_addr;
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];

    ret = omx__get_board_id(NULL, &board_index, board_name, &board_addr);
    if (ret == OMX_INVALID_PARAMETER)
      continue;
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to read board #%d id, %s\n", i, omx_strerror(ret));
      goto out;
    }

    assert(i == board_index);
    found++;

    printf("\n");
    omx__board_addr_sprintf(board_addr_str, board_addr);
    printf("Board #%d name %s addr %s\n",
	   i, board_name, board_addr_str);
    printf("==============================================\n");

    omx__peers_dump("  %d) %s %s\n");
  }

  return 0;

 out:
  return -1;
}
