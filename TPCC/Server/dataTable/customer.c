#include <assert.h>
#include <stdlib.h>
#include "customer.h"
#include "memory.h"
#include "tm.h"
#include "types.h"
#include "map.h"
#include "random.h"


/* =============================================================================
 * customer_alloc
 * =============================================================================
 */
customer_t*
customer_alloc (long C_ID,long C_D_ID,long C_W_ID,char* C_FIRST,char* C_MIDDLE,char* C_LAST,
		char* C_STREET_1,char* C_STREET_2,char* C_CITY,char* C_STATE,char* C_ZIP,char* C_PHONE,
		long C_SINCE,long C_CREDIT,float C_CREDIT_LIM,long C_DISCOUNT,float C_BALANCE,
		float C_YTD_PAYMENT,long C_PAYMENT_CNT,long C_DELIVERY_CNT,char* C_DATA)
{
    customer_t* customerPtr;

    customerPtr = (customer_t*)malloc(sizeof(customer_t));
    if(customerPtr==NULL)
    	assert("Error malloc customertPtr");

    customerPtr->C_ID=C_ID;
    customerPtr->C_D_ID=C_D_ID;
    customerPtr->C_W_ID=C_W_ID;
    //customerPtr->C_FIRST=C_FIRST;
    //customerPtr->C_MIDDLE=C_MIDDLE;
    customerPtr->C_LAST=C_LAST;
    //customerPtr->C_STREET_1=C_STREET_1;
    //customerPtr->C_STREET_2=C_STREET_2;
    //customerPtr->C_CITY=C_CITY;
    //customerPtr->C_STATE=C_STATE;
    //customerPtr->C_ZIP=C_ZIP;
    //customerPtr->C_PHONE=C_PHONE;
    customerPtr->C_SINCE=C_SINCE;
    customerPtr->C_BALANCE=-10.0;
    customerPtr->C_YTD_PAYMENT=10.0;
    customerPtr->C_PAYMENT_CNT=1;

    return customerPtr;
}

//generate the CLAST name according to clausule4.3.2.3 of Tpc-c
char *generate_CLAST(int seedClast){
	char stringCLast[10][10];
	strcpy(stringCLast[0],"BAR\0");
	strcpy(stringCLast[1],"OUGHT\0");
	strcpy(stringCLast[2],"ABLE\0");
	strcpy(stringCLast[3],"PRI\0");
	strcpy(stringCLast[4],"PRES\0");
	strcpy(stringCLast[5],"ESE\0");
	strcpy(stringCLast[6],"ANTI\0");
	strcpy(stringCLast[7],"CALLY\0");
	strcpy(stringCLast[8],"ACTION\0");
	strcpy(stringCLast[9],"EING\0");

	char *result;
	int app,module1,module2,module3;

	module3=seedClast%10;
	app=seedClast/10;
	module2=app%10;
	module1=app/10;
	result=(char *)malloc(strlen(stringCLast[module1])+strlen(stringCLast[module2])+strlen(stringCLast[module3])+1);
	result[0]='\0';

	strcat(result,stringCLast[module1]);
	strcat(result,stringCLast[module2]);
	strcat(result,stringCLast[module3]);
	return result;
}

//generate the CLAST name according to clausule4.3.2.3 of Tpc-c
char *TM_generate_CLAST(int seedClast)
{
	char stringCLast[10][10];
	strcpy(stringCLast[0],"BAR\0");
	strcpy(stringCLast[1],"OUGHT\0");
	strcpy(stringCLast[2],"ABLE\0");
	strcpy(stringCLast[3],"PRI\0");
	strcpy(stringCLast[4],"PRES\0");
	strcpy(stringCLast[5],"ESE\0");
	strcpy(stringCLast[6],"ANTI\0");
	strcpy(stringCLast[7],"CALLY\0");
	strcpy(stringCLast[8],"ACTION\0");
	strcpy(stringCLast[9],"EING\0");

	char *result;
	int app,module1,module2,module3;

	module3=seedClast%10;
	app=seedClast/10;
	module2=app%10;
	module1=app/10;
	result=(char *)TM_MALLOC(strlen(stringCLast[module1])+strlen(stringCLast[module2])+strlen(stringCLast[module3])+1);
	result[0]='\0';

	strcat(result,stringCLast[module1]);
	strcat(result,stringCLast[module2]);
	strcat(result,stringCLast[module3]);
	return result;
}

long
fill_customer(MAP_T *customerMap,random_t *randomPtr)
{
	const long DISTRICT=10;
	long id_d,id_c;
	long key=1;
	char* c_last;

	for(id_d=1;id_d<=DISTRICT;id_d++)
	{
		//add customer:3000 foreach district line
		for(id_c=1;id_c<=3000;id_c++)
		{
			customer_t* customerPtr;
			/*
			//C_FIRST [8..16]
			long lencfirts=(random_generate(randomPtr) % 8)+8;
			char c_first[lencfirts];
			memset(c_first,'a',lencfirts);*/

			//c_last in according to clausule 4.3.2.3
			if(id_c<=1000)
				c_last=generate_CLAST(id_c-1);
			else
				c_last=generate_CLAST(random_generate(randomPtr) % 999);
/*
			//C_STREET1 [10..20]
			long lencstreet1=(random_generate(randomPtr) % 10)+10;
			char c_street1[lencstreet1];
			memset(c_street1,'a',lencstreet1);

			//C_STREET1 [10..20]
			long lencstreet2=(random_generate(randomPtr) % 10)+10;
			char c_street2[lencstreet2];
			memset(c_street2,'a',lencstreet2);

			//C_CITY [10..20]
			long lenccity=(random_generate(randomPtr) % 10)+10;
			char c_city[lenccity];
			memset(c_city,'a',lenccity);*/

			//C_CREDIT GC 10%, other BC. 10%-->random[0..100], if <10 GC,else BC
			long chkCredit=random_generate(randomPtr) % 100;
			long c_credit;
			if(chkCredit<10)
				c_credit=0;
			else
				c_credit=1;

			//C_DISCOUNT
			float disc=(random_generate(randomPtr) % 5000)/1000;

			//customerPtr=customer_alloc (id_c,id_d,1,c_first,"OE",c_last,c_street1,c_street2,c_city,"aa","12341111","000000000000","12332123123",c_credit,50000.00,disc,-10.00,10.00,1,0,"aaaaaaaaaaaaaaaaaaaaaa");
			customerPtr=customer_alloc (id_c,id_d,1,NULL,"OE",c_last,NULL,NULL,NULL,"aa","12341111","000000000000",(long)"12332123123",c_credit,50000.00,disc,-10.00,10.00,1,0,"aaaaaaaaaaaaaaaaaaaaaa");

			if(!MAP_INSERT(customerMap,key,customerPtr))
				assert("Error MAP_INSERT customerPtr");

			key++;
		}
	}
	return key;
}

long
fill_customer2(MAP_T *customerMap,random_t *randomPtr, long id_d)
{
	long id_c;

		//add customer:3000 foreach district line
		for(id_c=1;id_c<=3000;id_c++)
		{
			customer_t* customerPtr;
			/*
			//C_FIRST [8..16]
			long lencfirts=(random_generate(randomPtr) % 8)+8;
			char c_first[lencfirts];
			memset(c_first,'a',lencfirts);*/

			//c_last in according to clausule 4.3.2.3
			char* c_last=generate_CLAST(random_generate(randomPtr) % 999);
/*
			//C_STREET1 [10..20]
			long lencstreet1=(random_generate(randomPtr) % 10)+10;
			char c_street1[lencstreet1];
			memset(c_street1,'a',lencstreet1);

			//C_STREET1 [10..20]
			long lencstreet2=(random_generate(randomPtr) % 10)+10;
			char c_street2[lencstreet2];
			memset(c_street2,'a',lencstreet2);

			//C_CITY [10..20]
			long lenccity=(random_generate(randomPtr) % 10)+10;
			char c_city[lenccity];
			memset(c_city,'a',lenccity);*/

			//C_CREDIT GC 10%, other BC. 10%-->random[0..100], if <10 GC,else BC
			long chkCredit=random_generate(randomPtr) % 100;
			long c_credit;
			if(chkCredit<10)
				c_credit=0;
			else
				c_credit=1;

			//C_DISCOUNT
			float disc=(random_generate(randomPtr) % 5000)/1000;

			//customerPtr=customer_alloc (id_c,id_d,1,c_first,"OE",c_last,c_street1,c_street2,c_city,"aa","12341111","000000000000","12332123123",c_credit,50000.00,disc,-10.00,10.00,1,0,"aaaaaaaaaaaaaaaaaaaaaa");
			// [BUG] - id_d never defined!
			customerPtr=customer_alloc (id_c,id_d,1,NULL,"OE",c_last,NULL,NULL,NULL,"aa","12341111","000000000000",(long)"12332123123",c_credit,50000.00,disc,-10.00,10.00,1,0,"aaaaaaaaaaaaaaaaaaaaaa");

			if(!MAP_INSERT(customerMap,id_c,customerPtr))
				assert("Error MAP_INSERT customerPtr");
	}
	return 0;
}


TM_CALLABLE
customer_t *
TM_getcustomer(TM_ARGDECL MAP_T * customerMapPtr,long W_ID,long D_ID,long C_ID)
{
	customer_t *customerPtr=NULL;
	long key;
	for(key=1;key<=30000;key++)
	{
		customerPtr=(customer_t *)TMMAP_FIND(customerMapPtr,key);
		if(customerPtr!=NULL){
			if(customerPtr->C_D_ID==D_ID && customerPtr->C_ID==C_ID && customerPtr->C_W_ID==W_ID)
			{
				break;
			}
		}
	}

	return customerPtr;
}

