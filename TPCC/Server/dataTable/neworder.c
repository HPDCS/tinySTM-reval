#include <stdio.h>
#include "neworder.h"
#include <assert.h>

neworder_t*
neworder_alloc (long NO_O_ID,long NO_D_ID,long NO_W_ID)
{
	neworder_t* neworderPtr=(neworder_t*)malloc(sizeof(neworder_t));

	if(neworderPtr==NULL)
		assert("Error malloc newOrderPtr");

	neworderPtr->NO_D_ID=NO_D_ID;
	neworderPtr->NO_O_ID=NO_O_ID;
	neworderPtr->NO_W_ID=NO_W_ID;

	return neworderPtr;
}

TM_CALLABLE
neworder_t*
TM_neworder_alloc (TM_ARGDECL long NO_O_ID,long NO_D_ID,long NO_W_ID)
{
	neworder_t* neworderPtr=(neworder_t*)TM_MALLOC(sizeof(neworder_t));

	if(neworderPtr==NULL)
		assert("Error TM_MALLOC neworderPtr");

	neworderPtr->NO_D_ID=NO_D_ID;
	neworderPtr->NO_O_ID=NO_O_ID;
	neworderPtr->NO_W_ID=NO_W_ID;

	return neworderPtr;
}

TM_CALLABLE
neworder_t*
TM_add_neworder(TM_ARGDECL list_t *neworderMapPtr,long NO_O_ID,long NO_D_ID,long NO_W_ID)
{
	neworder_t *neworderPtr= TM_neworder_alloc(TM_ARG NO_O_ID, NO_D_ID, NO_W_ID);
	assert(neworderPtr);

	if(!TMLIST_INSERT(neworderMapPtr, neworderPtr))
		assert("Error TMLIST_INSERT neworderPtr");
	return neworderPtr;
}

long
fill_neworder(list_t *neworderMap)
{
	long id_d;
	long id_o;
	neworder_t *neworderPtr;

	for(id_d=1;id_d<=10;id_d++)
	{
		for(id_o=2101;id_o<=3000;id_o++)
		{
			neworderPtr=neworder_alloc (id_o,id_d,0);

			if(!list_insert(neworderMap, neworderPtr))
				assert("Error list_insert neworderPtr");
		}
	}

	return list_getSize(neworderMap);
}
