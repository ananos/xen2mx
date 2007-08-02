/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "omx_lib.h"

#ifndef OMX_PEERS_DEFAULT_FILENAME
/* might be set by configure */
#define OMX_PEERS_DEFAULT_FILENAME "open-mx.peers"
#endif

#define OMX_PEERS_FILENAME_ENVVAR "OMX_PEERS_FILENAME"

#define OMX_PEERS_MAX_DEFAULT 1

struct omx_peer {
  int valid;
  char *hostname;
  uint64_t board_addr;
};

#define OMX_PEERS_FILELINELEN_MAX (10 + 1 + OMX_HOSTNAMELEN_MAX + OMX_BOARD_ADDR_STRLEN + 1)

static struct omx_peer * omx_peers = NULL;
static int omx_peers_max;

omx_return_t
omx__peers_read(void)
{
  char * omx_peers_filename = OMX_PEERS_DEFAULT_FILENAME;
  char line[OMX_PEERS_FILELINELEN_MAX];
  char *envvar;
  FILE *file;
  omx_return_t ret;
  int i;

  envvar = getenv(OMX_PEERS_FILENAME_ENVVAR);
  if (envvar != NULL) {
    printf("Using peers file '%s'\n", envvar);
    omx_peers_filename = envvar;
  }

  file = fopen(omx_peers_filename, "r");
  if (!file) {
    fprintf(stderr, "Provide a peers file '%s' (or update '%s' environment variable)\n",
	    omx_peers_filename, OMX_PEERS_FILENAME_ENVVAR);
    return OMX_BAD_ERROR;
  }

  if (omx_peers)
    free(omx_peers);
  omx_peers_max = OMX_PEERS_MAX_DEFAULT;
  omx_peers = malloc(sizeof(struct omx_peer));
  if (!omx_peers) {
    ret = OMX_NO_RESOURCES;
    goto out_with_file;
  }
  for(i=0; i<omx_peers_max; i++)
    omx_peers[i].valid = 0;

  while (fgets(line, OMX_PEERS_FILELINELEN_MAX, file)) {
    char hostname[OMX_HOSTNAMELEN_MAX];
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
      ret = OMX_INVALID_PARAMETER;
      goto out_with_file;
    }

    if (index >= omx_peers_max) {
      /* increasing peers array */
      struct omx_peer * new_peers;
      int new_peers_max = omx_peers_max;
      while (index >= new_peers_max)
	new_peers_max *= 2;
      new_peers = realloc(omx_peers, new_peers_max * sizeof(struct omx_peer));
      if (!new_peers) {
	ret = OMX_NO_RESOURCES;
	goto out_with_file;
      }
      for(i=omx_peers_max; i<new_peers_max; i++)
	new_peers[i].valid = 0;

      omx_peers = new_peers;
      omx_peers_max = new_peers_max;
    }

    /* is this peer index already in use? */
    if (omx_peers[index].valid) {
      fprintf(stderr, "Overriding host #%d %s with %s\n",
	      index, omx_peers[index].hostname, hostname);
    }

    /* add the new peer */
    omx_peers[index].valid = 1;
    omx_peers[index].hostname = strdup(hostname);
    omx_peers[index].board_addr = ((((uint64_t) addr_bytes[0]) << 40)
				   + (((uint64_t) addr_bytes[1]) << 32)
				   + (((uint64_t) addr_bytes[2]) << 24)
				   + (((uint64_t) addr_bytes[3]) << 16)
				   + (((uint64_t) addr_bytes[4]) << 8)
				   + (((uint64_t) addr_bytes[5]) << 0));
  }

  fclose(file);

  return OMX_SUCCESS;

 out_with_file:
  fclose(file);
  return ret;
}

omx_return_t
omx__peers_init(void)
{
  omx_return_t ret;

  ret = omx__peers_read();

  return ret;
}

omx_return_t
omx__peers_dump(const char * format)
{
  int i;

  for(i=0; i<omx_peers_max; i++)
    if (omx_peers[i].valid) {
      char addr_str[OMX_BOARD_ADDR_STRLEN];

      omx__board_addr_sprintf(addr_str, omx_peers[i].board_addr);
      printf(format, i, addr_str, omx_peers[i].hostname);
    }

  return OMX_SUCCESS;
}

omx_return_t
omx__peer_from_index(uint16_t index, uint64_t *board_addr, char **hostname)
{
  if (index >= omx_peers_max || !omx_peers[index].valid)
    return OMX_INVALID_PARAMETER;

  *board_addr = omx_peers[index].board_addr;
  *hostname = omx_peers[index].hostname;
  return OMX_SUCCESS;
}

omx_return_t
omx__peer_addr_to_index(uint64_t board_addr, uint16_t *index)
{
  int i;

  for(i=0; i<omx_peers_max; i++)
    if (omx_peers[i].valid && omx_peers[i].board_addr == board_addr) {
      *index = i;
      return OMX_SUCCESS;
    }

  return OMX_INVALID_PARAMETER;
}

omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr)
{
  int i;

  for(i=0; i<omx_peers_max; i++)
    if (omx_peers[i].valid && !strcmp(hostname, omx_peers[i].hostname)) {
      *board_addr = omx_peers[i].board_addr;
      return OMX_SUCCESS;
    }

  return OMX_INVALID_PARAMETER;
}

omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname)
{
  int i;

  for(i=0; i<omx_peers_max; i++)
    if (omx_peers[i].valid && board_addr == omx_peers[i].board_addr) {
      strcpy(hostname, omx_peers[i].hostname);
      return OMX_SUCCESS;
    }

  return OMX_INVALID_PARAMETER;
}
