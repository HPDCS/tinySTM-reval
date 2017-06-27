#include <stdio.h>
#include "map.h"
#include "random.h"

typedef struct stocktable {
    long S_I_ID;
    long S_W_ID;
    long S_QUANTITY;
    char* S_DIST_01;
    char* S_DIST_02;
    char* S_DIST_03;
    char* S_DIST_04;
    char* S_DIST_05;
    char* S_DIST_06;
    char* S_DIST_07;
    char* S_DIST_08;
    char* S_DIST_09;
    char* S_DIST_10;
    long S_YTD;
    long S_ORDER_CNT;
    long S_REMOTE_CNT;
    char* S_DATA;
} stocktable_t;

stocktable_t*
stocktable_alloc(long S_I_ID,long S_W_ID,long S_QUANTITY);
long
fill_stock(MAP_T *stockMap,random_t *randomPtr);
