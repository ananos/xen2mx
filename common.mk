ifeq ($(V),1)
	CMDPREFIX	=	
	CMDECHO	=	@true
	CMDECHOAFTERPREFIX	=	true
else
	CMDPREFIX	=	@
	CMDECHO	=	@echo
	CMDECHOAFTERPREFIX	=	echo
endif

