#include <stdio.h>
#include "history.h"
#include <time.h>
#include "tm.h"
#include "random.h"
#include <assert.h>

history_t*
history_alloc (long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT)
{
	history_t* historyPtr=(history_t*)malloc(sizeof(history_t));

	if(historyPtr==NULL)
		assert("Error malloc historyPtr");

	historyPtr->H_C_ID=H_C_ID;
	historyPtr->H_C_D_ID=H_C_D_ID;
	historyPtr->H_C_W_ID=H_C_W_ID;
	historyPtr->H_DATE=H_DATE;
	historyPtr->H_AMOUNT=H_AMOUNT;

	return historyPtr;
}

TM_CALLABLE
history_t*
TM_history_alloc (TM_ARGDECL long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT)
{
	history_t* historyPtr=(history_t*)TM_MALLOC(sizeof(history_t));

	if(historyPtr==NULL)
		assert("Error malloc historyPtr");

	historyPtr->H_C_ID=H_C_ID;
	historyPtr->H_C_D_ID=H_C_D_ID;
	historyPtr->H_C_W_ID=H_C_W_ID;
	historyPtr->H_DATE=H_DATE;
	historyPtr->H_AMOUNT=H_AMOUNT;

	return historyPtr;
}

TM_CALLABLE
history_t*
TM_add_history(TM_ARGDECL MAP_T *historyMapPtr,long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT,long key)
{
	history_t *historyPtr= TM_history_alloc(H_C_ID,H_C_D_ID,H_C_W_ID,H_DATE,H_AMOUNT);

	if(!TMMAP_INSERT(historyMapPtr, key, historyPtr))
		assert("Error TMMAP_INSERT historyPtr");
	return historyPtr;
}

long
fill_history(MAP_T *historyMap)
{

	time_t sec;
	sec = time (NULL);

	long id_c;
	long id_d;
	long id_h=1;
	for(id_d=1;id_d<=10;id_d++)
	{
		for(id_c=1;id_c<=3000;id_c++){
			history_t *historyPtr=history_alloc (id_c,id_d,0,sec,10);
			id_h++;
			if(!MAP_INSERT(historyMap,id_h,historyPtr))
				assert("Error MAP_INSERT historyPtr");
		}
	}
	return id_h;
}

