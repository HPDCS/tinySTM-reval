#include <stdio.h>
#include "tm.h"
#include "map.h"

typedef struct history {
    long H_C_ID;//H_C_W_ID, H_C_D_ID, H_C_ID) Foreign Key, references (C_W_ID, C_D_ID, C_ID)
    long H_C_D_ID;
    long H_C_W_ID;
    long H_D_ID;
    long H_W_ID;
    long H_DATE;
    float H_AMOUNT;
    char *H_DATA;
} history_t;

history_t*
history_alloc (long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT);

TM_CALLABLE
history_t*
TM_history_alloc (TM_ARGDECL long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT);

TM_CALLABLE
history_t*
TM_add_history(TM_ARGDECL MAP_T *historyMapPtr,long H_C_ID,long H_C_D_ID,long H_C_W_ID,long H_DATE,float H_AMOUNT,long key);

long
fill_history(MAP_T *historyMap);

#define TMHISTORY_ADD(historyMapPtr, H_C_ID, H_C_D_ID, H_C_W_ID, H_DATE, H_AMOUNT, key)		TM_add_history(TM_ARG historyMapPtr, H_C_ID, H_C_D_ID, H_C_W_ID, H_DATE, H_AMOUNT, key)
#define TMHISTORY_ALLOC( H_C_ID, H_C_D_ID, H_C_W_ID, H_DATE, H_AMOUNT)						TM_history_alloc (TM_ARG H_C_ID, H_C_D_ID, H_C_W_ID, H_DATE, H_AMOUNT);

