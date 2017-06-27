#include <stdio.h>
#include <stdlib.h>
#include "item.h"
#include "random.h"
#include "types.h"
#include "map.h"
#include <assert.h>

item_t* item_alloc(long I_ID,long I_IM_ID,char* I_NAME,float I_PRICE,char* I_DATA){

	item_t* itemPtr=(item_t*)malloc(sizeof(item_t));

	if(itemPtr==NULL)
		assert("Error malloc itemPtr");

	itemPtr->I_ID=I_ID;
	itemPtr->I_IM_ID=I_IM_ID;
	itemPtr->I_NAME=I_NAME;
	itemPtr->I_PRICE=I_PRICE;
	itemPtr->I_DATA=I_DATA;
	return itemPtr;
}


long
fill_item(MAP_T *itemMap,random_t *randomPtr)
{
	long rowNumber=100000;
	char iName[24];
	char iData[50];
	float iPrice;
	long imID;

	long id;

	for(id=1;id<=rowNumber;id++)
	{
		item_t *itemPtr;

		//fill item row
		imID = random_generate(randomPtr) % 10000;

		//lengh [14..24]=>10+lowbound(14)
		long leniname=(random_generate(randomPtr) % 10)+14;

		//lengh [26..50]=>24+lowbound(26)
		long lenidata=(random_generate(randomPtr) % 24)+26;

		memset(iName,'a',sizeof(iName));
		memset(iData,'a',sizeof(iData));

		iName[leniname]='\0';
		iData[lenidata]='\0';

		//price [1,00..100,00] =>[100..10000]/100=>(9900+lowbound(100))/100
		iPrice=(random_generate(randomPtr) % 9900);
		iPrice=iPrice+100;
		iPrice=iPrice/100;

		itemPtr=item_alloc(id,imID,iName,iPrice,iData);

		if(!MAP_INSERT(itemMap,id,itemPtr))
			assert("Error MAP_INSERT itemPtr");

	}
	return id;
}
