#include <stdio.h>
#include "types.h"
#include "map.h"
#include "random.h"

typedef struct item {
    long I_ID;//primary key
    long I_IM_ID;
    char* I_NAME;
    float I_PRICE;
    char* I_DATA;
} item_t;

item_t*
item_alloc(long I_ID,long I_IM_ID,char* I_NAME,float I_PRICE,char* I_DATA);

long
fill_item(MAP_T *itemMap,random_t *randomPtr);
