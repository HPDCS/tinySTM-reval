#include "tm.h"
#include "map.h"
#include "types.h"
#include "random.h"
#include "list.h"

#ifndef BTREE_H
#define BTREE_H
typedef struct order {
    long O_ID;//primary key (O_W_ID, O_D_ID, O_ID)
    long O_D_ID;//(O_W_ID, O_D_ID, O_C_ID) Foreign Key, references (C_W_ID, C_D_ID, C_ID)
    long O_W_ID;
    long O_C_ID;
    long O_ENTRY_D;
    long O_CARRIER_ID;
    long O_OL_CNT;
    long O_ALL_LOCAL;

    MAP_T* orderline;
}order_t;
#endif

/* =============================================================================
 * reservation_info_alloc
 * -- Returns NULL on failure
 * =============================================================================
 */
order_t*
order_alloc (long id_o, long id_d,long id_c,long id_w,long o_entry,long o_carrier,long o_ol_cnt,long o_all_local,random_t *randomPtr);

TM_CALLABLE
order_t*
TM_order_alloc(TM_ARGDECL long id_o, long id_d,long id_c,long id_w,long o_entry,long o_carrier,long o_ol_cnt,long o_all_local);

TM_CALLABLE
order_t*
TM_add_order(TM_ARGDECL MAP_T *orderMapPtr ,long id_o, long id_d,long id_c,long id_w,long o_entry,long o_carrier,long o_ol_cnt,long o_all_local,long key);

bool_t
fill_order(MAP_T *orderMap,random_t *randomPtr);

bool_t
fill_order2(MAP_T *orderMap,MAP_T *customerMap,random_t *randomPtr,long id_w,long id_d);


#define TMORDER_ADD(orderMapPtr,id_o,id_d,id_c,id_w,o_entry,o_carrier,o_ol_cnt,o_all_local,key)		TM_add_order(TM_ARG orderMapPtr,id_o,id_d,id_c,id_w,o_entry,o_carrier,o_ol_cnt,o_all_local,key)
#define TMORDER_ALLOC(id_o,id_d,id_c,id_w,o_entry,o_carrier,o_ol_cnt,o_all_local)					TM_order_alloc(TM_ARG d_o,id_d,id_c,id_w,o_entry,o_carrier,o_ol_cnt,o_all_local)

