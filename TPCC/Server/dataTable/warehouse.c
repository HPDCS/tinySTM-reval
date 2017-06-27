#include <stdio.h>
#include "warehouse.h"
#include "district.h"
#include "types.h"
#include "random.h"
#include "map.h"
#include "../manager.h"
#include <stdlib.h>
#include <assert.h>

warehouse_t*
warehouse_alloc (long id,float w_tax,random_t *randomPtr)
{
	warehouse_t * warehousePtr=(warehouse_t *)malloc(sizeof(warehouse_t));
	if(warehousePtr==NULL)
		assert("Impossibile allocare warehousePtr");

	warehousePtr->W_ID=id;
	warehousePtr->W_TAX=w_tax;
	warehousePtr->W_YTD=300000.0;

	warehousePtr->districtMap=MAP_ALLOC(NULL, NULL);

	fill_district(warehousePtr->districtMap,randomPtr);

	return warehousePtr;
}

long
fill_warehouse(MAP_T *warehouseMapPtr,random_t *randomPtr)
{

	float w_tax=(random_generate(randomPtr) % 2000);
	w_tax=w_tax/10000;
	warehouse_t* warehousePtr =warehouse_alloc(1,w_tax,randomPtr);
	long id=1;
	MAP_INSERT(warehouseMapPtr,id,warehousePtr);
	return 1;
}

TM_CALLABLE
warehouse_t*
TM_getwarehouse(TM_ARGDECL MAP_T * warehouseMapPtr,long W_ID)
{
	warehouse_t *warehousePtr=(warehouse_t*)TMMAP_FIND(warehouseMapPtr,W_ID);

	return warehousePtr;
}
