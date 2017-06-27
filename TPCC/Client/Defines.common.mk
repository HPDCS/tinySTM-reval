# ==============================================================================
#
# Defines.common.mk
#
# ==============================================================================


CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DMAP_USE_RBTREE
CFLAGS += -DCLIENT_MACRO

# To SYNCH send ops
#CFLAGS += -DSYNCH_SENDER

PROG := tpcc

SRCS += Client.c
#
OBJS := ${SRCS:.c=.o}


# ==============================================================================
#
# End of Defines.common.mk
#
# ==============================================================================
