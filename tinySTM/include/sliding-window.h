#pragma once

#include <stdlib.h>


#ifndef S_WINDOW_SIZE
	#define S_WINDOW_SIZE				10
#endif


typedef struct win_block {
	union {
		double					d_value;
		unsigned long long int	u_value;
	} value;
	union {
		double					d_sum;
		unsigned long long int	u_sum;
	} sum;
} win_block_t;

typedef struct window {
	double					mean;
	unsigned int			filled:1;
	unsigned int			position;
	win_block_t				window[S_WINDOW_SIZE];
} window_t;


static inline int winS_init(window_t** win_p, unsigned int win_n) {
	if (win_p == NULL || win_n == 0)
		return 1;
	if (((*win_p) = (window_t*) calloc(win_n, sizeof(window_t))) == NULL)
		return 1;
	return 0;
}

static inline int winS_fini(window_t** win_p) {
	if (win_p == NULL)
		return 1;
	if ((*win_p) == NULL)
		return 1;
	free((*win_p));
	(*win_p) = NULL;
	return 0;
}

static inline int winS_updateU(window_t* win, unsigned int n, unsigned long long int sample) {
	unsigned int new_position;
	if (win == NULL)
		return 1;
	new_position = (win[n].position + 1) % S_WINDOW_SIZE;
	if (win[n].filled==0 && new_position==0)
		win[n].filled = 1;
	win[n].window[new_position].sum.u_sum = (win[n].window[win[n].position].sum.u_sum - win[n].window[new_position].value.u_value) + sample;
	win[n].window[new_position].value.u_value = sample;
	win[n].position = new_position;
	win[n].mean = 0.0;
	return 0;
}

static inline int winS_updateD(window_t* win, unsigned int n, double sample) {
	unsigned int new_position;
	if (win == NULL)
		return 1;
	new_position = (win[n].position + 1) % S_WINDOW_SIZE;
	if (win[n].filled==0 && new_position==0)
		win[n].filled = 1;
	win[n].window[new_position].sum.d_sum = (win[n].window[win[n].position].sum.d_sum - win[n].window[new_position].value.d_value) + sample;
	win[n].window[new_position].value.d_value = sample;
	win[n].position = new_position;
	win[n].mean = 0.0;
	return 0;
}

static inline double winS_getMeanU(window_t* win, unsigned int n) {
	if (win == NULL)
		return 0.0;
	if (win[n].filled) {
		if (win[n].mean == 0.0)
			return (win[n].mean = ((double) win[n].window[win[n].position].sum.u_sum / (double) S_WINDOW_SIZE));
		else
			return win[n].mean;
	} else {
		if (win[n].mean == 0.0)
			return (win[n].mean = ((double) win[n].window[win[n].position].sum.u_sum / (double) win[n].position));
		else
			return win[n].mean;
	}
}

static inline double winS_getMeanD(window_t* win, unsigned int n) {
	if (win == NULL)
		return 0.0;
	if (win[n].filled) {
		if (win[n].mean == 0.0)
			return (win[n].mean = (win[n].window[win[n].position].sum.d_sum / (double) S_WINDOW_SIZE));
		else
			return win[n].mean;
	} else {
		if (win[n].mean == 0.0)
			return (win[n].mean = (win[n].window[win[n].position].sum.d_sum / (double) win[n].position));
		else
			return win[n].mean;
	}
}