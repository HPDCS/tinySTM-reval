#include <assert.h>
#include <stdlib.h>
#include "memory.h"
#include "order.h"
#include "tm.h"
#include "types.h"
#include "random.h"
#include "map.h"
#include "list.h"
#include "customer.h"
#include "orderline.h"

order_t*
order_alloc (long id_o, long id_d,long id_c,long id_w,long o_entry,long o_carrier,long o_ol_cnt,long o_all_local,random_t *randomPtr)
{
    order_t* orderPtr;

    orderPtr = (order_t*)malloc(sizeof(order_t));
    if (orderPtr == NULL) {
    	assert("Error malloc orderPtr");
    }

    orderPtr->O_ID=id_o;
    orderPtr->O_D_ID=id_d;
    orderPtr->O_C_ID=id_c;
    orderPtr->O_W_ID=id_w;
    orderPtr->O_ENTRY_D=o_entry;
    orderPtr->O_CARRIER_ID=o_carrier;
    orderPtr->O_OL_CNT=o_ol_cnt;
    orderPtr->O_ALL_LOCAL=o_all_local;

    //orderPtr->orderline=list_alloc(NULL);
    orderPtr->orderline=MAP_ALLOC(NULL, NULL);
    if(orderPtr->orderline==NULL)
    	assert("Error list_alloc orderlinePtr");

    fill_orderline(orderPtr->orderline,id_o,id_d,id_w,o_ol_cnt,randomPtr);

    return orderPtr;
}


TM_CALLABLE
order_t*
TM_order_alloc(TM_ARGDECL long id_o, long id_d,long id_c,long id_w,
		long o_entry,long o_carrier,long o_ol_cnt,long o_all_local)
{
	order_t* orderPtr;

	orderPtr=TM_MALLOC(sizeof(order_t));
	if(orderPtr == NULL)
	{
		assert("Error TM_MALLOC orderPtr");
	}

	orderPtr->O_ID=id_o;
	orderPtr->O_D_ID=id_d;
	orderPtr->O_C_ID=id_c;
	orderPtr->O_W_ID=id_w;
	orderPtr->O_ENTRY_D=o_entry;
	orderPtr->O_CARRIER_ID=o_carrier;
	orderPtr->O_OL_CNT=o_ol_cnt;
	orderPtr->O_ALL_LOCAL=o_all_local;

	//orderPtr->orderline=TMLIST_ALLOC(NULL);
	orderPtr->orderline=TMrbtree_alloc (NULL);
	assert(orderPtr->orderline!=NULL);

	return orderPtr;
}


TM_CALLABLE
order_t*
TM_add_order(TM_ARGDECL MAP_T *orderMapPtr ,long id_o, long id_d,\
		long id_c,long id_w,long o_entry,long o_carrier,\
		long o_ol_cnt,long o_all_local,long key_map)
{
	order_t *orderPtr= TM_order_alloc(TM_ARG id_o,  id_d,\
		 id_c, id_w, o_entry, o_carrier,\
		 o_ol_cnt, o_all_local);

	if(!TMMAP_INSERT(orderMapPtr, key_map, orderPtr))
		assert("Error TMMAP_INSERT orderPtr");
	return orderPtr;
}

/*
long
fill_order(MAP_T *orderMap,random_t *randomPtr)
{
	long rownumber=3000;
	long id=1;
	long id_o;
	long id_c;
	long id_d;
	long o_carr;
	long o_ol_cnt;

	time_t sec;
	sec = time (NULL);

	for(id_d=1;id_d<=10;id_d++)
	{
		for(id_o=1;id_o<=3000;id_o++)
		{
			if(id_o<2101)
				o_carr = (random_generate(randomPtr) % 10)+1;
			else
				o_carr=-1;

			o_ol_cnt=(random_generate(randomPtr) % 10)+5;

			order_t *orderPtr= order_alloc(id_o,id_d,id_c,0,sec,o_carr,o_ol_cnt,1,randomPtr);

			assert(MAP_INSERT(orderMap,id,orderPtr));

			id++;
		}
	}

	return id;
}*/

long
fill_order2(MAP_T *orderMap,MAP_T *customerMap,random_t *randomPtr,long id_w,long id_d)
{
	long rownumber=3000;
	long id_o;
	long id_c;
	long o_carr;
	long o_ol_cnt;

	time_t sec;
	sec = time (NULL);

	//random id_c
	id_c=(random_generate(randomPtr) % 3000)+1;
	for(id_o=1;id_o<=rownumber;id_o++)
	{
		if(id_o<2101)
			o_carr = (random_generate(randomPtr) % 10)+1;
		else
			o_carr=-1;

		o_ol_cnt=(random_generate(randomPtr) % 10)+5;

		//check customer ID
		assert(id_c<3001 && id_c>0);

		order_t *orderPtr= order_alloc(id_o,id_d,id_c,id_w,sec,o_carr,o_ol_cnt,1,randomPtr);

		if(!MAP_INSERT(orderMap,id_o,orderPtr))
			assert("Error MAP_INSERT orderMap");

		//add lastOrder to Customer
		customer_t * cPtr=MAP_FIND(customerMap,id_c);
		cPtr->lastOrder=orderPtr;

		//increment circular id customer
		id_c=(id_c+1)%3000;
		if(id_c==0)
		{
			id_c=3000;
		}

	}
	return id_o;
}
