#ifndef __mpoe_lib_h__
#define __mpoe_lib_h__

#include <string.h>

#include "mpoe_io.h"

#define MPOE_DEVNAME "/dev/mpoe"

/* FIXME: assertion to check MPOE_IF_NAMESIZE == IF_NAMESIZE */

static inline void
mpoe_mac_addr_copy(struct mpoe_mac_addr * dst,
		   struct mpoe_mac_addr * src)
{
	memcpy(dst, src, sizeof(struct mpoe_mac_addr));
}

static inline void
mpoe_mac_addr_set_bcast(struct mpoe_mac_addr * addr)
{
	memset(addr, 0xff, sizeof (struct mpoe_mac_addr));
}

#define MPOE_MAC_ADDR_STRLEN 18

static inline int
mpoe_mac_addr_sprintf(char * buffer, struct mpoe_mac_addr * addr)
{
	return sprintf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		       addr->hex[0], addr->hex[1], addr->hex[2],
		       addr->hex[3], addr->hex[4], addr->hex[5]);
}

static inline int
mpoe_mac_addr_sscanf(char * buffer, struct mpoe_mac_addr * addr)
{
	return sscanf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		      &addr->hex[0], &addr->hex[1], &addr->hex[2],
		      &addr->hex[3], &addr->hex[4], &addr->hex[5]);
}


#endif /* __mpoe_lib_h__ */
