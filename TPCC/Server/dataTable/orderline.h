#include <stdio.h>
#include "tm.h"
#include "map.h"
#include "random.h"


typedef struct orderline {
    long OL_O_ID;//Primary Key: (OL_W_ID, OL_D_ID, OL_O_ID, OL_NUMBER)
    long OL_D_ID;//(OL_W_ID, OL_D_ID, OL_O_ID) Foreign Key, references (O_W_ID, O_D_ID, O_ID)
    long OL_W_ID;//(OL_SUPPLY_W_ID, OL_I_ID) Foreign Key, references (S_W_ID, S_I_ID)
    long OL_NUMBER;
    long OL_I_ID;
    long OL_SUPPLY_W_ID;
    long OL_DELIVERY_D;
    long OL_QUANTITY;
    float OL_AMOUNT;
    char* OL_DIST_INFO;
} orderline_t;

orderline_t*
orderline_alloc(long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,
		long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT);

TM_CALLABLE
orderline_t*
TM_orderline_alloc(TM_ARGDECL long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT);

TM_CALLABLE
orderline_t*
TM_add_orderline(TM_ARGDECL MAP_T *orderlineMapPtr,long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT,char *OL_DIST_INFO,long key);

long
fill_orderline(MAP_T *orderlineMap,long id_o,long id_d,long id_w,long o_ol_cnt,random_t *randomPtr);

#define TMORDLINE_ADD(orderlineMapPtr,OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT,OL_DIST_INFO,key)	TM_add_orderline(TM_ARG orderlineMapPtr,OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT,OL_DIST_INFO,key)
#define TMORDLINE_ALLOC(OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT)									TM_orderline_alloc(TM_ARG OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT)
#define TMORDLINE_ADD2(orderlineMapPtr,OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT)		TM_add_orderline2(TM_ARG orderlineMapPtr,OL_O_ID,OL_D_ID,OL_W_ID,OL_NUMBER,OL_I_ID,OL_SUPPLY_W_ID,OL_DELIVERY_D,OL_QUANTITY,OL_AMOUNT)
