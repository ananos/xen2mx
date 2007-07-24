#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "mpoe_lib.h"

#define MPOE_PEERS_DEFAULT_FILENAME "mpoe.peers"
#define MPOE_PEERS_FILENAME_ENVVAR "MPOE_PEERS_FILENAME"

#define MPOE_PEERS_MAX_DEFAULT 1

struct mpoe_peer {
  int valid;
  char *hostname;
  uint64_t board_addr;
};

#define MPOE_PEERS_FILELINELEN_MAX (10 + 1 + MPOE_HOSTNAMELEN_MAX + MPOE_BOARD_ADDR_STRLEN + 1)

static struct mpoe_peer * mpoe_peers = NULL;
static int mpoe_peers_max;

mpoe_return_t
mpoe__peers_read(void)
{
  char * mpoe_peers_filename = MPOE_PEERS_DEFAULT_FILENAME;
  char line[MPOE_PEERS_FILELINELEN_MAX];
  char *envvar;
  FILE *file;
  mpoe_return_t ret;
  int i;

  envvar = getenv(MPOE_PEERS_FILENAME_ENVVAR);
  if (envvar != NULL) {
    printf("Using peers file '%s'\n", envvar);
    mpoe_peers_filename = envvar;
  }

  file = fopen(mpoe_peers_filename, "r");
  if (!file) {
    fprintf(stderr, "Provide a peers file '%s' (or update '%s' environment variable)\n",
	    mpoe_peers_filename, MPOE_PEERS_FILENAME_ENVVAR);
    return MPOE_BAD_ERROR;
  }

  if (mpoe_peers)
    free(mpoe_peers);
  mpoe_peers_max = MPOE_PEERS_MAX_DEFAULT;
  mpoe_peers = malloc(sizeof(struct mpoe_peer));
  if (!mpoe_peers) {
    ret = MPOE_NO_RESOURCES;
    goto out_with_file;
  }
  for(i=0; i<mpoe_peers_max; i++)
    mpoe_peers[i].valid = 0;

  while (fgets(line, MPOE_PEERS_FILELINELEN_MAX, file)) {
    char hostname[MPOE_HOSTNAMELEN_MAX];
    int index;
    int addr_bytes[6];

    /* ignore comments and empty lines */
    if (line[0] == '#' || strlen(line) == 1)
      continue;

    /* parse a line */
    if (sscanf(line, "%d\t%02x:%02x:%02x:%02x:%02x:%02x\t%s\n",
	       &index,
	       &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
	       &addr_bytes[3], &addr_bytes[4], &addr_bytes[5],
	       hostname)
	!= 8) {
      fprintf(stderr, "Unrecognized peer line '%s'\n", line);
      ret = MPOE_INVALID_PARAMETER;
      goto out_with_file;
    }

    if (index >= mpoe_peers_max) {
      /* increasing peers array */
      struct mpoe_peer * new_peers;
      int new_peers_max = mpoe_peers_max;
      while (index >= new_peers_max)
	new_peers_max *= 2;
      new_peers = realloc(mpoe_peers, new_peers_max * sizeof(struct mpoe_peer));
      if (!new_peers) {
	ret = MPOE_NO_RESOURCES;
	goto out_with_file;
      }
      for(i=mpoe_peers_max; i<new_peers_max; i++)
	mpoe_peers[i].valid = 0;
      mpoe_peers = new_peers;
      mpoe_peers_max = new_peers_max;
    }

    /* is this peer index already in use? */
    if (mpoe_peers[index].valid) {
      fprintf(stderr, "Overriding host #%d %s with %s\n",
	      index, mpoe_peers[index].hostname, hostname);
    }

    /* add the new peer */
    mpoe_peers[index].valid = 1;
    mpoe_peers[index].hostname = strdup(hostname);
    mpoe_peers[index].board_addr = ((((uint64_t) addr_bytes[0]) << 40)
				    + (((uint64_t) addr_bytes[1]) << 32)
				    + (((uint64_t) addr_bytes[2]) << 24)
				    + (((uint64_t) addr_bytes[3]) << 16)
				    + (((uint64_t) addr_bytes[4]) << 8)
				    + (((uint64_t) addr_bytes[5]) << 0));
  }

  fclose(file);

  return MPOE_SUCCESS;

 out_with_file:
  fclose(file);
  return ret;
}

mpoe_return_t
mpoe__peers_init(void)
{
  mpoe_return_t ret;

  ret = mpoe__peers_read();

  return ret;
}

mpoe_return_t
mpoe__peers_dump(const char * format)
{
  int i;

  for(i=0; i<mpoe_peers_max; i++)
    if (mpoe_peers[i].valid) {
      char addr_str[MPOE_BOARD_ADDR_STRLEN];

      mpoe_board_addr_sprintf(addr_str, mpoe_peers[i].board_addr);
      printf(format, i, addr_str, mpoe_peers[i].hostname);
    }

  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe__peer_from_index(uint16_t index, uint64_t *board_addr, char **hostname)
{
  if (index >= mpoe_peers_max || !mpoe_peers[index].valid)
    return MPOE_INVALID_PARAMETER;

  *board_addr = mpoe_peers[index].board_addr;
  *hostname = mpoe_peers[index].hostname;
  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe_hostname_to_nic_id(char *hostname,
			uint64_t *board_addr)
{
  int i;

  for(i=0; i<mpoe_peers_max; i++)
    if (!strcmp(hostname, mpoe_peers[i].hostname)) {
      *board_addr = mpoe_peers[i].board_addr;
      return MPOE_SUCCESS;
    }

  return MPOE_INVALID_PARAMETER;
}

mpoe_return_t
mpoe_nic_id_to_hostname(uint64_t board_addr,
			char *hostname)
{
  int i;

  for(i=0; i<mpoe_peers_max; i++)
    if (board_addr == mpoe_peers[i].board_addr) {
      strcpy(hostname, mpoe_peers[i].hostname);
      return MPOE_SUCCESS;
    }

  return MPOE_INVALID_PARAMETER;
}
