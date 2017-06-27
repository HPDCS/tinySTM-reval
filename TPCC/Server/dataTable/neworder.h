#include <stdio.h>
#include "tm.h"
#include "list.h"

typedef struct neworder {
    long NO_O_ID;//primarykey (NO_W_ID, NO_D_ID, NO_O_ID)
    long NO_D_ID;//(NO_W_ID, NO_D_ID, NO_O_ID) Foreign Key, references (O_W_ID, O_D_ID, O_ID)
    long NO_W_ID;
} neworder_t;

neworder_t*
neworder_alloc (long NO_O_ID,long NO_D_ID,long NO_W_ID);

TM_CALLABLE
neworder_t*
TM_neworder_alloc (TM_ARGDECL long NO_O_ID,long NO_D_ID,long NO_W_ID);

TM_CALLABLE
neworder_t*
TM_add_neworder(TM_ARGDECL list_t *neworderMapPtr,long NO_O_ID,long NO_D_ID,long NO_W_ID);

long
fill_neworder(list_t *neworderMap);

#define TMNWORDER_ALLOC(NO_O_ID, NO_D_ID, NO_W_ID)					TM_neworder_alloc (TM_ARG  NO_O_ID, NO_D_ID, NO_W_ID);
#define	TMNWORDER_ADD(neworderMapPtr, NO_O_ID, NO_D_ID, NO_W_ID)	TM_add_neworder(TM_ARG neworderMapPtr, NO_O_ID, NO_D_ID, NO_W_ID);
