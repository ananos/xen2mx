ECHO	      = echo
RM_F          = rm -f

OPEN_MX_DIR   =	$(abs_builddir)

installmoddir = $(prefix)/modules/$(OMX_LINUX_RELEASE)

nodist_installmod_DATA = open-mx.ko


noinst_HEADERS	= omx_common.h omx_debug.h omx_dma.h omx_endpoint.h	\
		  omx_hal.h omx_iface.h omx_misc.h omx_peer.h omx_reg.h	\
		  omx_shared.h omx_wire_access.h

EXTRA_DIST	= check_kernel_headers.sh				\
		  omx_dev.c omx_dma.c omx_event.c omx_iface.c		\
		  omx_main.c omx_peer.c omx_pull.c omx_raw.c omx_recv.c	\
		  omx_reg.c omx_send.c omx_shared.c

.PHONY: open-mx.ko
open-mx.ko:
	$(ECHO) "  MAKE     DRIVER"
	$(MAKE) -C $(OMX_LINUX_BUILD) M=$(OPEN_MX_DIR) $(OMX_KBUILD_ARGS)


clean-local:
	$(ECHO) "  CLEAN    DRIVER"
	$(MAKE) -C $(OMX_LINUX_BUILD) M=$(OPEN_MX_DIR) clean

distclean-local:
	$(RM_F) omx_checks.h
	$(RM_F) Module.markers
	$(RM_F) modules.order

