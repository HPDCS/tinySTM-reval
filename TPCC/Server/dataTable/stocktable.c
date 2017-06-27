#include <stdio.h>
#include "stocktable.h"
#include "types.h"
#include "random.h"
#include "map.h"
#include <assert.h>

stocktable_t*
stocktable_alloc(long S_I_ID,long S_W_ID,long S_QUANTITY)
{
	stocktable_t* stocktablePtr=(stocktable_t*)malloc(sizeof(stocktable_t));

	if(stocktablePtr==NULL)
		assert("Error malloc stocktablePtr");

	stocktablePtr->S_I_ID=S_I_ID;
	stocktablePtr->S_W_ID=S_W_ID;
	stocktablePtr->S_QUANTITY=S_QUANTITY;

	return stocktablePtr;
}

long
fill_stock(MAP_T *stockMap,random_t *randomPtr)
{
	long rowNumber=100000;
	long quantity;

	long id;
	for(id=1;id<=rowNumber;id++)
	{
		//random [10..100]
		quantity=(random_generate(randomPtr) % 90)+10;

		stocktable_t *stockPtr=stocktable_alloc(id,0,quantity);

		if(!MAP_INSERT(stockMap,id,stockPtr))
			assert("Error MAP_INSERT stockPtr");
	}

	//check
	for(id=1;id<=rowNumber;id++)
	{
		assert(MAP_FIND(stockMap,id));
	}
	return id;
}
