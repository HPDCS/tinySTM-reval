#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "map.h"
#include "pair.h"
#include "manager.h"
#include "tm.h"
#include "types.h"
#include "dataTable/customer.h"
#include "dataTable/order.h"
#include "dataTable/neworder.h"
#include "dataTable/history.h"
#include "dataTable/item.h"
#include "dataTable/warehouse.h"
#include "dataTable/orderline.h"
#include "dataTable/district.h"
#include "dataTable/stocktable.h"
#include "random.h"
#include "list.h"
#include "hashtable.h"

// #	define RO				1
// #	define RW				0
// #	define NO_RETRY			1
// #	define RETRY			0

// #	define ID_NWORDER		0
// #	define ID_PAYMENT		1
// #	define ID_ORDSTATUS 	2
// #	define ID_STOCKLEVEL	3
// #	define ID_DELIVERY		4

#define TM_ABORT()	do { \
		stm_abort(0); \
	} while(0)

/*
#define PROTECT \
	({ \
		control.stm_mode = 1; \
	})

#define UNPROTECT(val) \
	({ \
		if (val) { \
			if (!first_tx_operation) { \
				first_tx_operation = FIRST_DONE; \
			} \
			control.recently_validated = 1; \
		} \
		control.stm_mode = 0; \
		if (control.standing) { \
			control.standing = 0; \
		} \
	})

#define PROTECT_BLOCK(val, code, ret_type) \
	({ \
		control.stm_mode = 1; \
		ret_type ret = code; \
		if (val) { \
			if (!first_tx_operation) { \
				first_tx_operation = FIRST_DONE; \
			} \
			control.recently_validated = 1; \
		} \
		control.stm_mode = 0; \
		if (control.standing) { \
			control.standing = 0; \
		} \
		ret; \
	})
*/

#define PROTECT
#define UNPROTECT(val)
#define PROTECT_BLOCK(val, code, ret_type) ((ret_type)(code))

/* =============================================================================
 * tableAlloc
 * =============================================================================
 */

static MAP_T*
tableAlloc()
{
	return MAP_ALLOC(NULL, NULL);
}

static list_t*
listAlloc()
{
	return list_alloc(NULL);
}

/* =============================================================================
 * manager_alloc
 * =============================================================================
 */
manager_t*
manager_allocation()
{
	long key;
	manager_t* managerPtr;

	// idxOrders = (long*) malloc(sizeof(long));
	// lastOrders = (long*) malloc(1000*sizeof(long));

	//allocating manager structure
	managerPtr = (manager_t*)malloc(sizeof(manager_t));
	// puts("Allocation manager complete");
	assert(managerPtr != NULL);

	//alloc random structure
	managerPtr->randomPtr=random_alloc();
	random_seed (managerPtr->randomPtr, 42);
	assert(managerPtr->randomPtr != NULL);

	//initialize pthread
	//pthread_mutex_init(&(managerPtr->lockDelivery) , NULL);

	//alloc warehouse map
	managerPtr->warehouseMapPtr = tableAlloc();
	key=fill_warehouse(managerPtr->warehouseMapPtr,managerPtr->randomPtr);
	assert(managerPtr->warehouseMapPtr != NULL);
	printf("warehouse number:%ld\n",key);

	//alloc history map
	managerPtr->historyMapPtr = tableAlloc();
	managerPtr->next_key_history=fill_history(managerPtr->historyMapPtr);
	assert(managerPtr->historyMapPtr != NULL);
	printf("history number:%ld\n",managerPtr->next_key_history);

	//alloc item map
	managerPtr->itemMapPtr = tableAlloc();
	key=fill_item(managerPtr->itemMapPtr,managerPtr->randomPtr);
	assert(managerPtr->itemMapPtr != NULL);
	printf("item number:%ld\n",key);

	//alloc new order list
	managerPtr->neworderMapPtr = listAlloc();
	key=fill_neworder(managerPtr->neworderMapPtr);
	assert(managerPtr->neworderMapPtr != NULL);
	printf("neworder number:%ld\n",key);

	//alloc stock map
	managerPtr->stockMapPtr = tableAlloc();
	key=fill_stock(managerPtr->stockMapPtr,managerPtr->randomPtr);
	assert(managerPtr->stockMapPtr != NULL);
	printf("stock number:%ld\n",key);

	checkConsistency(managerPtr);

	return managerPtr;
}


void checkConsistency(manager_t* m)
{
	checkConsistency1(m);
}

//3.3.2.1 Entries in the WAREHOUSE and DISTRICT tables must satisfy the relationship:
//W_YTD = sum(D_YTD)
void checkConsistency1(manager_t *managerPtr)
{
	int i=0;
	do {
		warehouse_t *warehousePtr=MAP_FIND(managerPtr->warehouseMapPtr,i);
		if(warehousePtr==NULL){
			break;
		}

		float sum=0;
		int j;
		for(j=0;j<10;j++){
			district_t *districtPtr= MAP_FIND(warehousePtr->districtMap,j);
			if(districtPtr==NULL){
				break;
			}

			sum=sum+districtPtr->D_YTD;
		}

		PROTECT;
		assert(warehousePtr->W_YTD!=sum);
		UNPROTECT(0);

		i++;
	}while(TRUE);
}


/* =============================================================================
 * TRANSACTION INTERFACE
 * =============================================================================
 */

/* =============================================================================
 * neworder transaction
 * -- input parameters W_ID,D_ID and C_ID
 * -- Inser new order with 1% fail(rollback)
 * -- Returns -1 on error, 1 on success, 0 on rollback
 * =============================================================================
 */
TM_CALLABLE
long
manager_newordertransaction(TM_ARGDECL manager_t* managerPtr,long W_ID, long D_ID, long C_ID)
{
	//TM_BEGIN_CUSTOM(ID_NWORDER,RW,NO_RETRY);
	TM_BEGIN(0);
	time_t sec;
	random_t* randomPtr=managerPtr->randomPtr;

	//get warehouse,district,customer
	PROTECT;
	warehouse_t *warehousePtr=TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	UNPROTECT(1);

	if(warehousePtr==NULL){
		PROTECT;
		printf("***************************************1.Parametro warehouse errato %ld********************************\n",W_ID);
		UNPROTECT(0);
		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	district_t *districtPtr= TMMAP_FIND(warehousePtr->districtMap,D_ID);
	UNPROTECT(1);

	if(districtPtr==NULL){
		PROTECT;
		printf("***************************************1.Parametro district errato %ld********************************\n",D_ID);
		UNPROTECT(0);
		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	customer_t *customerPtr=MAP_FIND(districtPtr->customerMap,C_ID);
	UNPROTECT(0);

	if(customerPtr==NULL){
		PROTECT;
		printf("***************************************1.Parametro customer errato %ld********************************\n",C_ID);
		UNPROTECT(0);
		//TM_ABORT();
		TM_END();
		return -1;
	}

	//get W_TAX
	float w_tax=warehousePtr->W_TAX;
	//get D_TAX
	float d_tax=districtPtr->D_TAX;

	//get and increment D_NEXT_O_ID
	PROTECT;
	long d_next=TM_SHARED_READ(districtPtr->D_NEXT_O_ID);
	UNPROTECT(1);

	PROTECT;
	TM_SHARED_WRITE(districtPtr->D_NEXT_O_ID,d_next+1);
	UNPROTECT(1);

	//get C_DISCOUNT,C_LAST and C_CREDIT
	float c_discount=customerPtr->C_DISCOUNT;
	//char *c_last=customerPtr->C_LAST;
	//long c_credit=customerPtr->C_CREDIT;

	//insert ORDER and NEWORDER
	//generate o_ol_cnt
	PROTECT;
	long o_ol_cnt=(random_generate(randomPtr)%10)+5;
	UNPROTECT(0);

	PROTECT;
	sec=time(NULL);
	UNPROTECT(0);

	PROTECT;
	order_t *orderPtr=TMORDER_ADD(districtPtr->orderMap,d_next, \
			D_ID,C_ID,W_ID,sec,-1,o_ol_cnt,1,d_next);
	UNPROTECT(1);

//	TMNWORDER_ADD(managerPtr->neworderMapPtr,d_next,D_ID,W_ID);

	//set lastOrder Customer
	PROTECT;
	TM_SHARED_WRITE_P(customerPtr->lastOrder,orderPtr);
	UNPROTECT(1);

	//1% rollback
	PROTECT;
	long rbk=(random_generate(randomPtr)%100)+1;
	UNPROTECT(0);

	float total_amount=0;
	//create orderline
	long i;
	for(i=1;i<=o_ol_cnt;i++)
	{
		if(rbk==1 && i==(o_ol_cnt-1))
		{
//			break;
		}

		//seletotal_amountct random item
		PROTECT;
		long i_id=(random_generate(randomPtr)%100000)+1;
		UNPROTECT(0);

		PROTECT;
		item_t *itemPtr=TMMAP_FIND(managerPtr->itemMapPtr,i_id);
		UNPROTECT(1);

//		/* DEBUG :: TTMAP_FIND could return NULL */
//		if (itemPtr == NULL)
//			continue;
//		/* DEBUG :: itemPtr->I_PRICE causes SIGSEGV */

		float i_price=itemPtr->I_PRICE;
		//char *i_name=itemPtr->I_NAME;
		//char *i_data=itemPtr->I_DATA;

		//select stocktable
		PROTECT;
		stocktable_t *stocktablePtr=TMMAP_FIND(managerPtr->stockMapPtr,i_id);
		UNPROTECT(1);

//		/* DEBUG :: TTMAP_FIND could return NULL */
//		if (stocktablePtr == NULL)
//			continue;
//		/* DEBUG :: stocktablePtr causes SIGSEGV */

		//generate random quantity
		PROTECT;
		long quantity=(random_generate(randomPtr)%10)+1;
		UNPROTECT(0);

		//decrease quantity
		PROTECT;
		long s_quantity=(long)TM_SHARED_READ(stocktablePtr->S_QUANTITY);
		UNPROTECT(1);

		s_quantity=s_quantity-quantity;
		if(s_quantity<=10)
			s_quantity+=91;

		PROTECT;
		TM_SHARED_WRITE(stocktablePtr->S_QUANTITY,s_quantity);
		UNPROTECT(1);

		PROTECT;
		TM_SHARED_WRITE(stocktablePtr->S_YTD,(long)TM_SHARED_READ(stocktablePtr->S_YTD)+quantity);
		UNPROTECT(1);

		PROTECT;
		TM_SHARED_WRITE(stocktablePtr->S_ORDER_CNT,(long)TM_SHARED_READ(stocktablePtr->S_ORDER_CNT)+1);
		UNPROTECT(1);

		//add ordeline row
		float ol_amount=quantity*i_price;
		total_amount+=ol_amount;

		//orderline_t* orderlinePtr=TMORDLINE_ADD2(orderPtr->orderline,d_next,D_ID,W_ID,i,i_id,W_ID,-1,quantity,ol_amount);
		PROTECT;
		orderline_t* orderlinePtr=TMORDLINE_ADD(orderPtr->orderline,d_next,D_ID,W_ID,i,i_id,W_ID,-1,quantity,ol_amount,NULL,i);
		UNPROTECT(1);

		if(orderlinePtr==NULL)
		{
			TM_ABORT();
		}
	}

	if(rbk==1)
	{
//		TM_ABORT();
	}

	//total amount with tax
	total_amount=(total_amount*(1-c_discount)*(1+w_tax+d_tax));
	TM_END();

	return 1;
}


/* =============================================================================
 * payment transaction
 * -- input parameters W_ID,D_ID,C_ID and H_AMOUNT
 * -- Payment order
 * -- Returns -1 on error, 1 on success
 * =============================================================================
 */

TM_CALLABLE
long
manager_paymenttransaction(TM_ARGDECL manager_t* managerPtr,long W_ID, long D_ID, long C_ID,float H_AMOUNT,char *c_last)
{
	//TM_BEGIN_CUSTOM(ID_PAYMENT,RW,NO_RETRY);
	TM_BEGIN(1);
	// time_t sec;
	random_t* randomPtr=managerPtr->randomPtr;
	char *c_last22;

	c_last22 = NULL;

	//get warehouse,district,customer
	PROTECT;
	warehouse_t *warehousePtr=TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	UNPROTECT(1);

	if(warehousePtr==NULL){
		PROTECT;
		printf("***************************************2.Parametro warehouse errato %ld********************************\n",W_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	district_t *districtPtr=TMMAP_FIND(warehousePtr->districtMap,D_ID);
	UNPROTECT(1);

	if(districtPtr==NULL)
	{
		PROTECT;
		printf("***************************************2.Parametro district errato %ld********************************\n",D_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	customer_t *customerPtr=NULL;
	if(C_ID!=-1)
	{
		PROTECT;
		customerPtr=TMMAP_FIND(districtPtr->customerMap,C_ID);
		UNPROTECT(1);
	}
	else{
		PROTECT;
		int clastseed=abs((int)random_generate(randomPtr)%999);
		UNPROTECT(0);

		PROTECT;
		c_last22=TM_generate_CLAST(clastseed);
		UNPROTECT(1);

		long i;
		customer_t *customerPtrApp=NULL;
		for(i=1;i<=3000;i++)
		{
			PROTECT;
			customerPtrApp=TMMAP_FIND(districtPtr->customerMap,i);
			UNPROTECT(1);

			if( PROTECT_BLOCK(0,strncmp(c_last22,customerPtrApp->C_LAST, 100),int) == 0)
			{
				customerPtr=customerPtrApp;
				break;

			}
		}
	}

	if(customerPtr==NULL)
	{
		PROTECT;
		printf("***************************************2.Customer non trovato (%s)********************************\n",c_last22);
		UNPROTECT(0);

		TM_RESTART();
	}

	//update W_YTD
	PROTECT;
	TM_SHARED_WRITE_F(warehousePtr->W_YTD,(float)TM_SHARED_READ_F(warehousePtr->W_YTD)+H_AMOUNT);
	UNPROTECT(1);

	//update D_YTD
	PROTECT;
	TM_SHARED_WRITE_F(districtPtr->D_YTD,(float)TM_SHARED_READ_F(districtPtr->D_YTD)+H_AMOUNT);
	UNPROTECT(1);

	//update C_BALANCE,C_YTD_PAYMENT and C_PAYMENT_CNT
	PROTECT;
	TM_SHARED_WRITE_F(customerPtr->C_BALANCE,(float)TM_SHARED_READ_F(customerPtr->C_BALANCE)-H_AMOUNT);
	UNPROTECT(1);

	PROTECT;
	TM_SHARED_WRITE_F(customerPtr->C_YTD_PAYMENT,(float)TM_SHARED_READ_F(customerPtr->C_YTD_PAYMENT)+H_AMOUNT);
	UNPROTECT(1);

	PROTECT;
	TM_SHARED_WRITE(customerPtr->C_PAYMENT_CNT,(long)TM_SHARED_READ(customerPtr->C_PAYMENT_CNT)+1);
	UNPROTECT(1);

	//insert new row in history

/*	sec=time (NULL);
	long key_next_h=(long)TM_SHARED_READ(managerPtr->next_key_history);
	history_t* historyPtr=TMHISTORY_ADD(managerPtr->historyMapPtr,C_ID,D_ID,W_ID,sec,H_AMOUNT,key_next_h);

	if(historyPtr==NULL){
		printf("***************************************2.History Ptr NULL********************************\n");
		TM_RESTART();
	}

	TM_SHARED_WRITE(managerPtr->next_key_history,key_next_h+1);
*/	TM_END();
	return 1;

}

/* =============================================================================
 * orderstatus transaction
 * -- input parameters W_ID,D_ID and C_ID
 * -- extract the status of the order
 * -- Returns NULL on error, array long on success
 * =============================================================================
 */
TM_CALLABLE
long
manager_orderstatustransaction(TM_ARGDECL manager_t* managerPtr,long W_ID, long D_ID, long C_ID)
{
	TM_BEGIN(2);
	long result[50];

	//get warehouse,district,customer
	PROTECT;
	warehouse_t *warehousePtr=(warehouse_t *)TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	UNPROTECT(1);

	if(warehousePtr==NULL){
		PROTECT;
		printf("***************************************3.Parametro warehouse errato %ld********************************\n",W_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	district_t *districtPtr= (district_t *)TMMAP_FIND(warehousePtr->districtMap,D_ID);
	UNPROTECT(1);

	if(districtPtr==NULL)
	{
		PROTECT;
		printf("***************************************3.Parametro district errato %ld********************************\n",D_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	customer_t *customerPtr=(customer_t *)TMMAP_FIND(districtPtr->customerMap,C_ID);
	UNPROTECT(1);

	if(customerPtr==NULL)
	{
		PROTECT;
		printf("***************************************3.Parametro customer errato %ld********************************\n",C_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	//get last order
	//long last_ord=TM_SHARED_READ(districtPtr->D_NEXT_O_ID)-1;
	//modifica find last customer order
	//order_t* lastOrderPtr=NULL;
	/*order_t* temp=NULL;
	while(last_ord>0)
	{
		temp=(order_t *)TMMAP_FIND(districtPtr->orderMap,last_ord);
		//if(temp!=NULL && temp->O_C_ID==C_ID){
			lastOrderPtr=temp;
			break;
		//}
		last_ord--;
	}
	temp=NULL;*/

	PROTECT;
	order_t* lastOrderPtr=(order_t*)TM_SHARED_READ_P(customerPtr->lastOrder);
	UNPROTECT(1);

	if(lastOrderPtr!=NULL)
	{
		result[0]=lastOrderPtr->O_OL_CNT;
		int index=1;

		int count;
		for(count=1;count<=result[0];count++)
		{
			PROTECT;
			orderline_t* orderLinePtr=TMMAP_FIND(lastOrderPtr->orderline,count);
			UNPROTECT(1);

			if(orderLinePtr==NULL)
				TM_RESTART();

			result[index]=orderLinePtr->OL_I_ID;
			index++;

			PROTECT;
			if((long)TM_SHARED_READ(orderLinePtr->OL_DELIVERY_D)<0)
				result[index]=1;
			else
				result[index]=0;
			UNPROTECT(1);

			index++;
		}
	}

	TM_END();
	return 1;
}

/* =============================================================================
 * stock level transaction
 * -- input parameters W_ID,D_ID and treshold
 * -- Count items that are low than the treshold with 1% fail(rollback)
 * -- Returns -1 on error
 * =============================================================================
 */
TM_CALLABLE
long
manager_stockleveltransaction(TM_ARGDECL manager_t* managerPtr,long W_ID, long D_ID, long treshold)
{
	TM_BEGIN(3);

	PROTECT;
	warehouse_t *warehousePtr=TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	UNPROTECT(1);

	if(warehousePtr==NULL)
	{
		PROTECT;
		printf("***************************************4.Parametro warehouse errato %ld********************************\n",W_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	district_t *districtPtr= TMMAP_FIND(warehousePtr->districtMap,D_ID);
	UNPROTECT(1);

	if(districtPtr==NULL)
	{
		PROTECT;
		printf("***************************************4.Parametro district errato %ld********************************\n",D_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	long countStockLevel=0;

	unsigned long key;
	int item[100000];

	PROTECT;
	memset(item,0,sizeof(int)*100000);
	UNPROTECT(0);

	stocktable_t *stocktablePtr;
	orderline_t *orderlinePtr;
	order_t *orderPtr;

	for(
		key=( PROTECT_BLOCK(1,TM_SHARED_READ(districtPtr->D_NEXT_O_ID),stm_word_t) - 20 );
		key<PROTECT_BLOCK(1,TM_SHARED_READ(districtPtr->D_NEXT_O_ID),stm_word_t);
		key++
		){
		//get order
		orderPtr = NULL;

		PROTECT;
		do {
			orderPtr=TMMAP_FIND(districtPtr->orderMap,key);
		} while (orderPtr == NULL);
		UNPROTECT(1);

		PROTECT;
		assert(orderPtr);
		UNPROTECT(0);

		unsigned int count;
		for(count=1;count<=(long)PROTECT_BLOCK(1,TM_SHARED_READ(orderPtr->O_OL_CNT),stm_word_t);count++)
		{
			PROTECT;
			orderlinePtr=TMMAP_FIND(orderPtr->orderline,count);
			UNPROTECT(1);

			if(orderlinePtr!=NULL){
				//distinct!!!
				if(item[orderlinePtr->OL_I_ID]==0){
					PROTECT;
					stocktablePtr=(stocktable_t *)TMMAP_FIND(managerPtr->stockMapPtr,orderlinePtr->OL_I_ID);
					UNPROTECT(1);

					if((long)PROTECT_BLOCK(1,TM_SHARED_READ(stocktablePtr->S_QUANTITY),stm_word_t)<treshold){
						countStockLevel++;
					}
					item[orderlinePtr->OL_I_ID]=1;
				}
			}
		}
	}

// stock_level_tx_end:
	TM_END();

	return 1;
}

/* =============================================================================
 * delivery transaction
 * -- input parameters W_ID,and carrier
 * -- Delivery one order x district
 * -- Returns -1 on error, 1 on success
 * =============================================================================
 */

#if defined(DELIVERY_BIGTX)
/*
TM_CALLABLE
long
manager_deliverytransaction(TM_ARGDECL manager_t* managerPtr,long W_ID,long O_CARRIER_ID)
{
	//sumalating queue delivery, only one thread x time can access queue!!!
	//pthread_mutex_lock(&(managerPtr->lockDelivery));

	TM_BEGIN();
	long d_id;
	time_t sec;
	customer_t *customerPtr;
	neworder_t* neworderPtr;
	order_t *orderPtr;
	orderline_t *orderlinePtr;
	list_iter_t iter;

	warehouse_t *warehousePtr=(warehouse_t *)TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	if(warehousePtr==NULL)
	{
		printf("************************5.Warehouse non corretta %ld***********\n",W_ID);
		//TM_ABORT();
		TM_END();
		return -1;
	}

	district_t *districtPtr;
	TM_BEGIN();
	for(d_id=1;d_id<=10;d_id++)
	{
		//get district
		districtPtr=(district_t *)TMMAP_FIND(warehousePtr->districtMap,d_id);

		TMLIST_ITER_RESET(&iter,managerPtr->neworderMapPtr);
		while(TMLIST_ITER_HASNEXT(&iter,managerPtr->neworderMapPtr))
		{
			neworderPtr=TMLIST_ITER_NEXT(&iter,managerPtr->neworderMapPtr);
			if(neworderPtr!=NULL){
				if(neworderPtr->NO_D_ID==d_id && neworderPtr->NO_W_ID==W_ID)
				{
					//get order from district
					orderPtr=(order_t*)TMMAP_FIND(districtPtr->orderMap,neworderPtr->NO_O_ID);
					if(orderPtr!=NULL){
						float total_amount=0.0;
						long c_id=orderPtr->O_C_ID;
						TM_SHARED_WRITE(orderPtr->O_CARRIER_ID,O_CARRIER_ID);

						//find orderline
						int count;
						for(count=1;count<=TM_SHARED_READ(orderPtr->O_OL_CNT);count++)
						{
							orderlinePtr=TMMAP_FIND(orderPtr->orderline,count);
							if(orderlinePtr!=NULL){
								sec=time(NULL);
								TM_SHARED_WRITE(orderlinePtr->OL_DELIVERY_D,sec);
								total_amount+=TM_SHARED_READ_F(orderlinePtr->OL_AMOUNT);
							}
						}

						//update customer balance and delivery
						customerPtr=TMMAP_FIND(districtPtr->customerMap,c_id);
						if(customerPtr!=NULL){
							TM_SHARED_WRITE_F(customerPtr->C_BALANCE,((float)TM_SHARED_READ_F(customerPtr->C_BALANCE))+total_amount);
							TM_SHARED_WRITE(customerPtr->C_DELIVERY_CNT,((long)TM_SHARED_READ(customerPtr->C_DELIVERY_CNT))+1);
						}
					}
					TMLIST_REMOVE(managerPtr->neworderMapPtr,neworderPtr);
					neworderPtr=NULL;
					break;
				}
			}
		}//while end

	}

	TM_END();

	//pthread_mutex_unlock(&(managerPtr->lockDelivery));
	return 1;
}
*/
#else
TM_CALLABLE
long
manager_deliverytransaction(TM_ARGDECL manager_t* managerPtr,long W_ID,long O_CARRIER_ID)
{
	//sumalating queue delivery, only one thread x time can access queue!!!
	//pthread_mutex_lock(&(managerPtr->lockDelivery));
	volatile long d_id;
	time_t sec;
	customer_t *customerPtr;
	neworder_t* neworderPtr;
	order_t *orderPtr;
	orderline_t *orderlinePtr;
	list_iter_t iter;

	for(d_id=1;d_id<=10;d_id++)
	{
		TM_BEGIN(4);

		PROTECT;
		warehouse_t *warehousePtr=(warehouse_t *)TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
		UNPROTECT(1);

		if(warehousePtr==NULL)
		{
			PROTECT;
			printf("************************5.Warehouse non corretta %ld***********\n",W_ID);
			UNPROTECT(0);

			//TM_ABORT();
			TM_END();
			return -1;
		}

		district_t *districtPtr;


		//get district
		PROTECT;
		districtPtr=(district_t *)TMMAP_FIND(warehousePtr->districtMap,d_id);
		UNPROTECT(1);

		TMLIST_ITER_RESET(&iter,managerPtr->neworderMapPtr);
		//	[BUG] typeof(managerPtr->neworderMapPtr) is list_t*, typeof(iter) is list_iter_t!
		//iter = managerPtr->neworderMapPtr;

		while(PROTECT_BLOCK(1,TMLIST_ITER_HASNEXT(&iter,managerPtr->neworderMapPtr),bool_t))
		{
			PROTECT;
			neworderPtr=TMLIST_ITER_NEXT(&iter,managerPtr->neworderMapPtr);
			UNPROTECT(1);

			if(neworderPtr!=NULL){
				if(neworderPtr->NO_D_ID==d_id && neworderPtr->NO_W_ID==W_ID)
				{
					//get order from district
					PROTECT;
					orderPtr=(order_t*)TMMAP_FIND(districtPtr->orderMap,neworderPtr->NO_O_ID);
					UNPROTECT(1);

					if(orderPtr!=NULL){
						float total_amount=0.0;
						long c_id=orderPtr->O_C_ID;

						PROTECT;
						TM_SHARED_WRITE(orderPtr->O_CARRIER_ID,O_CARRIER_ID);
						UNPROTECT(1);

						//find orderline
						unsigned int count;
						for(count=1;count<=PROTECT_BLOCK(1,TM_SHARED_READ(orderPtr->O_OL_CNT),stm_word_t);count++)
						{
							PROTECT;
							orderlinePtr=TMMAP_FIND(orderPtr->orderline,count);
							UNPROTECT(1);

							if(orderlinePtr!=NULL){
								sec=time(NULL);

								PROTECT;
								TM_SHARED_WRITE(orderlinePtr->OL_DELIVERY_D,sec);
								UNPROTECT(1);

								PROTECT;
								total_amount+=TM_SHARED_READ_F(orderlinePtr->OL_AMOUNT);
								UNPROTECT(1);
							}
						}

						//update customer balance and delivery
						PROTECT;
						customerPtr=TMMAP_FIND(districtPtr->customerMap,c_id);
						UNPROTECT(1);

						if(customerPtr!=NULL){
							PROTECT;
							TM_SHARED_WRITE_F(customerPtr->C_BALANCE,((float)TM_SHARED_READ_F(customerPtr->C_BALANCE))+total_amount);
							UNPROTECT(1);

							PROTECT;
							TM_SHARED_WRITE(customerPtr->C_DELIVERY_CNT,((long)TM_SHARED_READ(customerPtr->C_DELIVERY_CNT))+1);
							UNPROTECT(1);
						}
					}

					PROTECT;
					TMLIST_REMOVE(managerPtr->neworderMapPtr,neworderPtr);
					UNPROTECT(1);

					neworderPtr=NULL;
					break;
				}
			}
		}//while end
		TM_END();
	}
	//pthread_mutex_unlock(&(managerPtr->lockDelivery));
	return 1;
}
#endif

#ifdef NEWQUAGLIA
/* =============================================================================
 * newquaglia transaction :D
 * =============================================================================
 */

TM_CALLABLE
long
manager_newquagliatransaction(TM_ARGDECL manager_t* managerPtr, long W_ID, long D_ID, long C_ID, long treshold, long NNEWORDERTXS)
{

	TM_BEGIN(5);

	struct random_data dbuf;
	struct random_data cbuf;
	struct random_data tbuf;
	memset( &dbuf, 0, sizeof( struct random_data ) );
	memset( &cbuf, 0, sizeof( struct random_data ) );
	memset( &tbuf, 0, sizeof( struct random_data ) );


	char dstate[32];
	char cstate[32];
	char tstate[32];

	int ii;
	int32_t id_d, id_c, tr;

	PROTECT;
	initstate_r(D_ID    , dstate, 32, &dbuf);
	initstate_r(C_ID    , cstate, 32, &cbuf);
	initstate_r(treshold, tstate, 32, &tbuf);
	UNPROTECT(0);

	for (ii = 0; ii < 1; ++ii) {

		PROTECT;
		random_r(&dbuf, &id_d);
		UNPROTECT(0);
		id_d = (id_d % 10) + 1;

		PROTECT;
		random_r(&cbuf, &id_c);
		UNPROTECT(0);
		id_c = (id_c % 3000) + 1;

		/* =============================================================================
		 * neworder transaction
		 * -- input parameters W_ID,D_ID and C_ID
		 * -- Inser new order with 1% fail(rollback)
		 * -- Returns -1 on error, 1 on success, 0 on rollback
		 * =============================================================================
		 */

		time_t sec;
		random_t* randomPtr=managerPtr->randomPtr;

		//get warehouse,district,customer
		PROTECT;
		warehouse_t *warehousePtr=TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
		UNPROTECT(1);

		if(warehousePtr==NULL){
			PROTECT;
			printf("***************************************1.Parametro warehouse errato %ld********************************\n",W_ID);
			UNPROTECT(0);
			TM_END();
			return -1;
		}

		PROTECT;
		district_t *districtPtr= TMMAP_FIND(warehousePtr->districtMap,id_d);
		UNPROTECT(1);

		if(districtPtr==NULL){
			PROTECT;
			printf("***************************************1.Parametro district errato %d********************************\n",id_d);
			UNPROTECT(0);
			//TM_ABORT();
			TM_END();
			return -1;
		}

		PROTECT;
		customer_t *customerPtr=MAP_FIND(districtPtr->customerMap,id_c);
		UNPROTECT(0);

		if(customerPtr==NULL){
			PROTECT;
			printf("***************************************1.Parametro customer errato %d********************************\n",id_c);
			UNPROTECT(0);
			//TM_ABORT();
			TM_END();
			return -1;
		}

		//get W_TAX
		float w_tax=warehousePtr->W_TAX;
		//get D_TAX
		float d_tax=districtPtr->D_TAX;

		//get and increment D_NEXT_O_ID
		PROTECT;
		long d_next=TM_SHARED_READ(districtPtr->D_NEXT_O_ID);
		UNPROTECT(1);

		PROTECT;
		TM_SHARED_WRITE(districtPtr->D_NEXT_O_ID,d_next+1);
		UNPROTECT(1);

		//get C_DISCOUNT,C_LAST and C_CREDIT
		float c_discount=customerPtr->C_DISCOUNT;
		//char *c_last=customerPtr->C_LAST;
		//long c_credit=customerPtr->C_CREDIT;

		//insert ORDER and NEWORDER
		//generate o_ol_cnt
		PROTECT;
		long o_ol_cnt=(random_generate(randomPtr)%10)+5;
		UNPROTECT(0);

		PROTECT;
		sec=time(NULL);
		UNPROTECT(0);

		PROTECT;
		order_t *orderPtr=TMORDER_ADD(districtPtr->orderMap,d_next, \
				id_d,id_c,W_ID,sec,-1,o_ol_cnt,1,d_next);
		UNPROTECT(1);

		/* PATCH x RBTREE */
	/*	long d_next;
		do {
			PROTECT;
			d_next = (long) random_generate(randomPtr);
			UNPROTECT(0);
		} while (PROTECT_BLOCK(1, TMMAP_CONTAINS(districtPtr->orderMap, d_next), long));

		PROTECT;
		order_t *orderPtr=TMORDER_ADD(districtPtr->orderMap,d_next, \
				D_ID,C_ID,W_ID,sec,-1,o_ol_cnt,1,d_next);
		UNPROTECT(1);
	*/
	//	TMNWORDER_ADD(managerPtr->neworderMapPtr,d_next,D_ID,W_ID);

		/* PATCH x RBTREE */
	/*	PROTECT;
		long idx = (TM_SHARED_READ(idxOrders[0]) + 1) % 1000;
		UNPROTECT(1);
		PROTECT;
		TM_SHARED_WRITE(lastOrders[idx], d_next);
		UNPROTECT(1);
		PROTECT;
		TM_SHARED_WRITE(idxOrders[0], idx);
		UNPROTECT(1);
	*/
		//set lastOrder Customer
		PROTECT;
		TM_SHARED_WRITE_P(customerPtr->lastOrder,orderPtr);
		UNPROTECT(1);

		//1% rollback
		PROTECT;
		long rbk=(random_generate(randomPtr)%100)+1;
		UNPROTECT(0);

		float total_amount=0;
		//create orderline
		long i;
		for(i=1;i<=o_ol_cnt;i++)
		{
			if(rbk==1 && i==(o_ol_cnt-1))
			{
	//			break;
			}

			//seletotal_amountct random item
			PROTECT;
			long i_id=(random_generate(randomPtr)%100000)+1;
			UNPROTECT(0);

			PROTECT;
			item_t *itemPtr=TMMAP_FIND(managerPtr->itemMapPtr,i_id);
			UNPROTECT(1);

	//		/* DEBUG :: TTMAP_FIND could return NULL */
	//		if (itemPtr == NULL)
	//			continue;
	//		/* DEBUG :: itemPtr->I_PRICE causes SIGSEGV */

			float i_price=itemPtr->I_PRICE;
			//char *i_name=itemPtr->I_NAME;
			//char *i_data=itemPtr->I_DATA;

			//select stocktable
			PROTECT;
			stocktable_t *stocktablePtr=TMMAP_FIND(managerPtr->stockMapPtr,i_id);
			UNPROTECT(1);

	//		/* DEBUG :: TTMAP_FIND could return NULL */
	//		if (stocktablePtr == NULL)
	//			continue;
	//		/* DEBUG :: stocktablePtr causes SIGSEGV */

			//generate random quantity
			PROTECT;
			long quantity=(random_generate(randomPtr)%10)+1;
			UNPROTECT(0);

			//decrease quantity
			PROTECT;
			long s_quantity=(long)TM_SHARED_READ(stocktablePtr->S_QUANTITY);
			UNPROTECT(1);

			s_quantity=s_quantity-quantity;
			if(s_quantity<=10)
				s_quantity+=91;

			PROTECT;
			TM_SHARED_WRITE(stocktablePtr->S_QUANTITY,s_quantity);
			UNPROTECT(1);

			PROTECT;
			TM_SHARED_WRITE(stocktablePtr->S_YTD,(long)TM_SHARED_READ(stocktablePtr->S_YTD)+quantity);
			UNPROTECT(1);

			PROTECT;
			TM_SHARED_WRITE(stocktablePtr->S_ORDER_CNT,(long)TM_SHARED_READ(stocktablePtr->S_ORDER_CNT)+1);
			UNPROTECT(1);

			//add ordeline row
			float ol_amount=quantity*i_price;
			total_amount+=ol_amount;

			//orderline_t* orderlinePtr=TMORDLINE_ADD2(orderPtr->orderline,d_next,D_ID,W_ID,i,i_id,W_ID,-1,quantity,ol_amount);
			PROTECT;
			orderline_t* orderlinePtr=TMORDLINE_ADD(orderPtr->orderline,d_next,id_d,W_ID,i,i_id,W_ID,-1,quantity,ol_amount,NULL,i);
			UNPROTECT(1);

			if(orderlinePtr==NULL)
			{
				TM_ABORT();
			}
		}

		if(rbk==1)
		{
	//		TM_ABORT();
		}

		//total amount with tax
		total_amount=(total_amount*(1-c_discount)*(1+w_tax+d_tax));
		// TM_END();

	}

	/* =============================================================================
	 * stock level transaction
	 * -- input parameters W_ID,D_ID and treshold
	 * -- Count items that are low than the treshold with 1% fail(rollback)
	 * -- Returns -1 on error
	 * =============================================================================
	 */

	PROTECT;
	random_r(&dbuf, &id_d);
	UNPROTECT(0);
	id_d = (id_d % 10) + 1;

	PROTECT;
	random_r(&tbuf, &tr);
	UNPROTECT(0);
	tr = (treshold % 10) + 10;

	PROTECT;
	warehouse_t *warehousePtr=TMMAP_FIND(managerPtr->warehouseMapPtr,W_ID);
	UNPROTECT(1);

	if(warehousePtr==NULL)
	{
		PROTECT;
		printf("***************************************4.Parametro warehouse errato %ld********************************\n",W_ID);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	PROTECT;
	district_t *districtPtr= TMMAP_FIND(warehousePtr->districtMap,id_d);
	UNPROTECT(1);

	if(districtPtr==NULL)
	{
		PROTECT;
		printf("***************************************4.Parametro district errato %d********************************\n",id_d);
		UNPROTECT(0);

		//TM_ABORT();
		TM_END();
		return -1;
	}

	long countStockLevel=0;

	long key;
	int item[100000];

	PROTECT;
	memset(item,0,sizeof(int)*100000);
	UNPROTECT(0);

	stocktable_t *stocktablePtr;
	orderline_t *orderlinePtr;
	order_t *orderPtr;

	for(
		key=( PROTECT_BLOCK(1,TM_SHARED_READ(districtPtr->D_NEXT_O_ID),stm_word_t) - 20 );
		key<PROTECT_BLOCK(1,TM_SHARED_READ(districtPtr->D_NEXT_O_ID),stm_word_t);
		key++
		){
		//get order
		orderPtr = NULL;

		PROTECT;
		do {
			orderPtr=TMMAP_FIND(districtPtr->orderMap,key);
		} while(orderPtr == NULL);
		UNPROTECT(1);

//		/* DEBUG :: TTMAP_FIND could return NULL */
//		if (orderPtr == NULL)
//			continue;
//		/* DEBUG :: orderPtr causes SIGSEGV */

		PROTECT;
		assert(orderPtr);
		UNPROTECT(0);

		int count;
		for(count=1;count<=(long)PROTECT_BLOCK(1,TM_SHARED_READ(orderPtr->O_OL_CNT),stm_word_t);count++)
		{
			PROTECT;
			orderlinePtr=TMMAP_FIND(orderPtr->orderline,count);
			UNPROTECT(1);

			if(orderlinePtr!=NULL){
				//distinct!!!
				if(item[orderlinePtr->OL_I_ID]==0){
					PROTECT;
					stocktablePtr=(stocktable_t *)TMMAP_FIND(managerPtr->stockMapPtr,orderlinePtr->OL_I_ID);
					UNPROTECT(1);

//					/* DEBUG :: TTMAP_FIND could return NULL */
//					if (stocktablePtr == NULL)
//						continue;
//					/* DEBUG :: stocktablePtr causes SIGSEGV */

					if((long)PROTECT_BLOCK(1,TM_SHARED_READ(stocktablePtr->S_QUANTITY),stm_word_t)<tr){
						countStockLevel++;
					}
					item[orderlinePtr->OL_I_ID]=1;
				}
			}
		}
	}

	TM_END();

	return 1;
}
#endif

/* =============================================================================
 *
 * End of manager.c
 *
 * =============================================================================
 */
