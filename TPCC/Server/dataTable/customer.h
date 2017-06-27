#include "tm.h"
#include "map.h"
#include "types.h"
#include "random.h"
#include "order.h"

struct customer {
    long C_ID;//primary key(C_W_ID,C_D_ID,C_ID)
    long C_D_ID;
    long C_W_ID;
    char* C_FIRST;
    char* C_MIDDLE;
    char* C_LAST;
    char* C_STREET_1;
    char* C_STREET_2;
    char* C_CITY;
    char* C_STATE;
    char* C_ZIP;
    char* C_PHONE;
    long C_SINCE;//date format timestamp
    long C_CREDIT;//0=GC=good,1=BC=bad
    float C_CREDIT_LIM;
    float C_DISCOUNT;
    float C_BALANCE;
    float C_YTD_PAYMENT;
    long C_PAYMENT_CNT;
    long C_DELIVERY_CNT;
    char* C_DATA;
    order_t *lastOrder;
};

typedef struct customer customer_t;
/* =============================================================================
 * customer_alloc
 * =============================================================================
 */
customer_t*
customer_alloc (long C_ID,long C_D_ID,long C_W_ID,char* C_FIRST,char* C_MIDDLE,char* C_LAST,
		char* C_STREET_1,char* C_STREET_2,char* C_CITY,char* C_STATE,char* C_ZIP,char* C_PHONE,
		long C_SINCE,long C_CREDIT,float C_CREDIT_LIM,long C_DISCOUNT,float C_BALANCE,
		float C_YTD_PAYMENT,long C_PAYMENT_CNT,long C_DELIVERY_CNT,char* C_DATA);

long
fill_customer(MAP_T *customerMap,random_t *randomPtr);

long
fill_customer2(MAP_T *customerMap,random_t *randomPtr, long id_d);


TM_CALLABLE
customer_t *
TM_getcustomer(TM_ARGDECL MAP_T* customerMapPtr,long W_ID,long D_ID,long C_ID);

char *
TM_generate_CLAST(int seedClast);
#define TMCUSTOMER_GET(customerMapPtr,W_ID,D_ID,C_ID)	TM_getcustomer(TM_ARG customerMapPtr,W_ID,D_ID,C_ID);

/* =============================================================================
 *
 * End of customer.h
 *
 * =============================================================================
 */
