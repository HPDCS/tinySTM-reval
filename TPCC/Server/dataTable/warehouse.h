#include <stdio.h>
#include "types.h"
#include "map.h"
#include "random.h"

typedef struct warehouse {
    long W_ID;//primary key
    char *W_NAME;//name warehouse
    char *W_STREET_1;//address
    char *W_STREET_2;
    char *W_CITY;
    char *W_STATE;
    char *W_ZIP;
    float W_TAX;
    float W_YTD;
    MAP_T *districtMap;
} warehouse_t;


long
fill_warehouse(MAP_T *warehouseMapPtr,random_t *randomPtr);

TM_CALLABLE
warehouse_t*
TM_getwarehouse(TM_ARGDECL MAP_T * warehouseMapPtr,long W_ID);

#define TMWAREHOUSE_GET(warehouseMapPtr,W_ID)	TM_getwarehouse(TM_ARG warehouseMapPtr,W_ID);

