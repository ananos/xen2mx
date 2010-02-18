SUBDIRS =

# arch=64
if ON_ARCHSIZE_64

# arch=64 / multilib
if OMX_MULTILIB

# arch=64 / multilib / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib64-debug lib32-debug
endif

# arch=64 / multilib / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib64 lib32
endif

# arch=64 / no multilib
else

# arch=64 / no multilib / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib64-debug
endif

# arch=64 / no multilib / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib64
endif

# arch=64 / { multilib | no multilib }
endif

# arch=32
else

# arch=32 / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib32-debug
endif

# arch=32 / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib32
endif

# arch={32|64}
endif

noinst_HEADERS = dlmalloc.h omx_hal.h omx_lib.h	omx__mx_compat.h	\
		 omx_raw.h omx_request.h omx_segments.h omx_threads.h	\
		 omx_types.h omx_valgrind.h

EXTRA_DIST = omx__mx_lib.version
