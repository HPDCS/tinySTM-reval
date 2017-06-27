#include <stdio.h>
#include "map.h"
#include "random.h"

typedef struct district {
    long D_ID;//primary key(D_ID,D_W_ID)
    long D_W_ID;//foreign key to W_ID
    char* D_NAME;
    char* D_STREET_1;
    char* D_STREET_2;
    char* D_CITY;
    char* D_STATE;
    char* D_ZIP;
    float D_TAX;
    float D_YTD;
    long D_NEXT_O_ID;//next available order number
    MAP_T *customerMap;
    MAP_T *orderMap;
} district_t;

long
fill_district(MAP_T *districtMap,random_t *randomPtr);

TM_CALLABLE
district_t*
TM_getdistrict(TM_ARGDECL MAP_T * districtMapPtr,long W_ID,long D_ID);

#define TMDISTRICT_GET(districtMapPtr,W_ID,D_ID)		TM_getdistrict(TM_ARG districtMapPtr,W_ID,D_ID);
