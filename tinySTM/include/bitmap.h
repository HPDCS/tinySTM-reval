#pragma once

#include <stdlib.h>


#define BITMAP_GET_BIT(bm,p)		(bm.bitmap[p/8] & (1 << (p%8)))
#define BITMAP_SET_BIT(bm,p)		 bm.bitmap[p/8] = (bm.bitmap[p/8] | (1 << (p%8)))
#define BITMAP_RESET_BIT(bm,p)		 bm.bitmap[p/8] = (bm.bitmap[p/8] & ~(1 << (p%8)))


typedef struct bitmap {
	unsigned int		size;
	unsigned char*		bitmap;
} bitmap_t;


static inline int bitmap_init(bitmap_t* bm, unsigned int s) {
	if (bm == NULL || s == 0)
		return 1;
	bm->size = s;
	if ((bm->bitmap = (unsigned char*) calloc((s/8)+1, sizeof(char))) == NULL)
		return 1;
	return 0;
}

static inline int bitmap_fini(bitmap_t* bm) {
	if (bm == NULL)
		return 1;
	if (bm->bitmap == NULL)
		return 1;
	free(bm->bitmap);
	bm->bitmap = NULL;
	bm->size = 0;
	return 0;
}

static inline void bitmap_reset(bitmap_t* bm) {
	int b;
	if (bm == NULL)
		return;
	if (bm->size == 0 || bm->bitmap == NULL)
		return;
	for (b=0; b<(bm->size/8)+1; b++)
		bm->bitmap[b] = 0;
	return;
}