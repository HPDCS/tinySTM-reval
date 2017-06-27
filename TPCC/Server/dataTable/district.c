#include <stdio.h>
#include "district.h"
#include "types.h"
#include "map.h"
#include "random.h"
#include "customer.h"
#include "order.h"
#include "../manager.h"
#include <assert.h>

district_t*
district_alloc (long D_ID,long D_W_ID,float D_TAX,random_t *randomPtr)
{
	district_t* districtPtr=(district_t*)malloc(sizeof(district_t));

	if(districtPtr==NULL)
		assert("Impossibile allocare districtPtr");

	districtPtr->D_ID=D_ID;
	districtPtr->D_W_ID=D_W_ID;
	districtPtr->D_NEXT_O_ID=3001;
	districtPtr->D_TAX=D_TAX;
	districtPtr->D_YTD=30000.0;

	districtPtr->customerMap=MAP_ALLOC(NULL, NULL);
	if(districtPtr->customerMap==NULL)
		assert("Error malloc customerMap");

	fill_customer2(districtPtr->customerMap,randomPtr,D_ID);

	districtPtr->orderMap=MAP_ALLOC(NULL, NULL);
	if(districtPtr->orderMap==NULL)
		assert("Error malloc orderMap");

	fill_order2(districtPtr->orderMap,districtPtr->customerMap,randomPtr,D_W_ID,D_ID);

	return districtPtr;
}

long
fill_district(MAP_T *districtMap,random_t *randomPtr)
{
	long rowNumber=10;

	long id;
	for(id=1;id<=rowNumber;id++)
	{
		float d_tax=(random_generate(randomPtr) % 2000);
		d_tax=d_tax/10000;
		district_t *districtPtr=district_alloc(id,1,d_tax,randomPtr);

		if(!MAP_INSERT(districtMap,id,districtPtr))
			assert("Error MAP_INSERT districtPtr");
	}
	return id;
}

TM_CALLABLE
district_t*
TM_getdistrict(TM_ARGDECL MAP_T * districtMapPtr,long W_ID,long D_ID)
{
	district_t *districtPtr=NULL;

	long key;
	for(key=1;key<=10;key++){
		districtPtr=(district_t*)TMMAP_FIND(districtMapPtr,key);
		if(districtPtr!=NULL)
		{
			if(districtPtr->D_ID==D_ID && districtPtr->D_W_ID==W_ID)
				break;
		}
	}

	return districtPtr;
}
