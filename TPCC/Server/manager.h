#include "map.h"
#include "tm.h"
#include "types.h"
#include "list.h"
#include "random.h"

typedef struct manager {
	MAP_T* warehouseMapPtr;
	MAP_T* historyMapPtr;
	MAP_T* itemMapPtr;
	list_t* neworderMapPtr;
	MAP_T* stockMapPtr;
	long next_key_history;
	random_t *randomPtr;
	pthread_mutex_t lockDelivery;
} manager_t;

manager_t *
manager_allocation();

void checkConsistency(manager_t* m);
void checkConsistency1(manager_t *managerPtr);

TM_CALLABLE
long
manager_newordertransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long C_ID);

TM_CALLABLE
long
manager_paymenttransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long C_ID, float H_AMOUNT, char *c_last);

TM_CALLABLE
long
manager_orderstatustransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long C_ID);

TM_CALLABLE
long
manager_stockleveltransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long treshold);

TM_CALLABLE
long
manager_deliverytransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long O_CARRIER_ID);

#ifdef NEWQUAGLIA
TM_CALLABLE
long
manager_newquagliatransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long C_ID, long treshold, long NNEWORDERTXS);
#endif

#define TMMANAGER_NEWORDER(managerPtr,W_ID,D_ID,C_ID) \
	manager_newordertransaction(TM_ARG managerPtr,W_ID,D_ID,C_ID)
#define TMMANAGER_PAYMENT(managerPtr,W_ID,D_ID,C_ID,H_AMOUNT,c_flag) \
	manager_paymenttransaction(TM_ARG managerPtr,W_ID,D_ID,C_ID,H_AMOUNT,c_last)
#define TMMANAGER_ORDSTATUS(managerPtr,W_ID,D_ID,C_ID) \
	manager_orderstatustransaction(TM_ARG managerPtr,W_ID,D_ID,C_ID)
#define TMMANAGER_STOCKLEVEL(managerPtr,W_ID,D_ID,treshold) \
	manager_stockleveltransaction(TM_ARG managerPtr,W_ID,D_ID,treshold)
#define TMMANAGER_DELIVERY(managerPtr,W_ID,O_CARRIER_ID) \
	manager_deliverytransaction(TM_ARG managerPtr,W_ID,O_CARRIER_ID);
#ifdef NEWQUAGLIA
#define TMMANAGER_NEWQUAGLIA(managerPtr,W_ID,D_ID,C_ID,treshold,NNEWORDERTXS) \
	manager_newquagliatransaction(TM_ARG managerPtr,W_ID,D_ID,C_ID,treshold,NNEWORDERTXS)
#endif
