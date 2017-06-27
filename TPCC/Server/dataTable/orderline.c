#include <stdio.h>
#include "orderline.h"
#include "order.h"
#include "random.h"
#include "list.h"
#include <assert.h>

orderline_t*
orderline_alloc(long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,
		long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT)
{
	orderline_t* orderlinePtr=(orderline_t*)malloc(sizeof(orderline_t));

	if(orderlinePtr==NULL)
		assert("Error malloc orderlinePtr");

	orderlinePtr->OL_O_ID=OL_O_ID;
	orderlinePtr->OL_D_ID=OL_D_ID;
	orderlinePtr->OL_W_ID=OL_W_ID;
	orderlinePtr->OL_NUMBER=OL_NUMBER;
	orderlinePtr->OL_I_ID=OL_I_ID;
	orderlinePtr->OL_SUPPLY_W_ID=OL_SUPPLY_W_ID;
	orderlinePtr->OL_DELIVERY_D=OL_DELIVERY_D;
	orderlinePtr->OL_QUANTITY=OL_QUANTITY;
	orderlinePtr->OL_AMOUNT=OL_AMOUNT;

	return orderlinePtr;
}

TM_CALLABLE
orderline_t*
TM_orderline_alloc(TM_ARGDECL long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,
		long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT)
{
	orderline_t* orderlinePtr=(orderline_t*)TM_MALLOC(sizeof(orderline_t));

	if(orderlinePtr==NULL)
		assert("Error TM_MALLOC orderlinePtr");

	orderlinePtr->OL_O_ID=OL_O_ID;
	orderlinePtr->OL_D_ID=OL_D_ID;
	orderlinePtr->OL_W_ID=OL_W_ID;
	orderlinePtr->OL_NUMBER=OL_NUMBER;
	orderlinePtr->OL_I_ID=OL_I_ID;
	orderlinePtr->OL_SUPPLY_W_ID=OL_SUPPLY_W_ID;
	orderlinePtr->OL_DELIVERY_D=OL_DELIVERY_D;
	orderlinePtr->OL_QUANTITY=OL_QUANTITY;
	orderlinePtr->OL_AMOUNT=OL_AMOUNT;

	return orderlinePtr;
}

TM_CALLABLE
orderline_t*
TM_add_orderline(TM_ARGDECL MAP_T *orderlineMapPtr,long OL_O_ID,long OL_D_ID,
		long OL_W_ID,long OL_NUMBER,long OL_I_ID,long OL_SUPPLY_W_ID,
		long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT,char OL_DIST_INFO[24],long key)
{
	orderline_t *orderlinePtr= TM_orderline_alloc(TM_ARG OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER,OL_I_ID, OL_SUPPLY_W_ID, OL_DELIVERY_D, OL_QUANTITY, OL_AMOUNT);

	if(!TMMAP_INSERT(orderlineMapPtr, key, orderlinePtr))
		assert("Error TMMAP_INSERT orderlinePtr");
	return orderlinePtr;
}

TM_CALLABLE
orderline_t*
TM_add_orderline2(TM_ARGDECL list_t *orderlineMapPtr,long OL_O_ID,long OL_D_ID,long OL_W_ID,long OL_NUMBER,long OL_I_ID,long OL_SUPPLY_W_ID,long OL_DELIVERY_D,long OL_QUANTITY,float OL_AMOUNT)
{
	orderline_t *orderlinePtr= TM_orderline_alloc(TM_ARG  OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER,OL_I_ID, OL_SUPPLY_W_ID, OL_DELIVERY_D, OL_QUANTITY, OL_AMOUNT);

	if(!TMLIST_INSERT(orderlineMapPtr,orderlinePtr))
		assert("Error TMLIST_INSERT orderlinePtr");
	return orderlinePtr;
}


long
fill_orderline(MAP_T *orderlineMap,long id_o,long id_d,long id_w,long o_ol_cnt,random_t *randomPtr)
{
	long ol_deliv;
	float ol_amount;
	orderline_t *orderlinePtr;

	long i;
	for(i=1;i<=o_ol_cnt;i++)
	{
		if(id_o<2101){
			ol_deliv = (random_generate(randomPtr) % 10)+1;
			ol_amount=0.00;
		}
		else{
			ol_deliv=-1;
			ol_amount=((random_generate(randomPtr) % 999999)+1)/100;
		}


		long id_item=(random_generate(randomPtr) % 100000)+1;
		orderlinePtr =orderline_alloc(id_o,id_d,id_w,i,id_item,id_w,ol_deliv,5,ol_amount);

		if(!MAP_INSERT(orderlineMap,i,orderlinePtr))
			assert("Error list_insert orderlinePtr");
	}
	return 0;
}


long
fill_orderline_array(orderline_t **orderlineMap,long id_o,long id_d,long id_w,long o_ol_cnt,random_t *randomPtr)
{
	long ol_deliv;
	float ol_amount;
	orderline_t *orderlinePtr;

		long i;
		for(i=1;i<=o_ol_cnt;i++)
		{
			if(id_o<2101){
				ol_deliv = (random_generate(randomPtr) % 10)+1;
				ol_amount=0.00;
			}
			else{
				ol_deliv=-1;
				ol_amount=((random_generate(randomPtr) % 999999)+1)/100;
			}


			long id_item=(random_generate(randomPtr) % 100000)+1;
			orderlinePtr =orderline_alloc(id_o,id_d,id_w,i,id_item,id_w,ol_deliv,5,ol_amount);

			orderlineMap[o_ol_cnt-1]=orderlinePtr;
		}
	return 0;
}
