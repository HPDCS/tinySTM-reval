# ==============================================================================
#
# Defines.common.mk
#
# ==============================================================================


CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DMAP_USE_RBTREE
CFLAGS += -UDELIVERY_BIGTX

# In lib/ MACRO
CFLAGS += -DSERVER_MACRO

# For M.M.
CFLAGS += -DGC_PERIOD=1

ifeq ($(NBB),1)
  CFLAGS += -DUSE_NBBLIST=1
  SRCS += core/nbblist.c
endif

PROG := tpcc

SRCS += \
	mm/taskpool.c \
	core/priority.c \
	scheduler/scheduler.c \
	stats/stats.c \
	manager.c \
	stm_threadpool.c \
	Server.c \
	dataTable/customer.c \
	dataTable/district.c \
	dataTable/history.c \
	dataTable/item.c \
	dataTable/neworder.c \
	dataTable/order.c \
	dataTable/orderline.c \
	dataTable/stocktable.c \
	dataTable/warehouse.c \
	$(LIB)/list.c \
	$(LIB)/pair.c \
	$(LIB)/mt19937ar.c \
	$(LIB)/random.c \
	$(LIB)/rbtree.c \
	$(LIB)/hashtable.c \
	$(LIB)/thread.c
#
OBJS := ${SRCS:.c=.o}


# ==============================================================================
#
# End of Defines.common.mk
#
# ==============================================================================
