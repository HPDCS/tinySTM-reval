#pragma once
#ifndef _CONTROLBUFFER_LIB
#define _CONTROLBUFFER_LIB

typedef enum {
	MODE_STUBBORN = 0,
	MODE_STUBBORN_HEURISTIC,
	MODE_QUIESCENT_HEURISTIC,
	MODE_QUIESCENT_MODEL
} delivery_mode;

typedef struct _control_buffer {
	unsigned short int		tx_on;					// True if the ET can be delivered
	unsigned short int		standing;				// True if there's a tick standing
	unsigned short int		recently_validated;		// True if recently validated

	unsigned long int*		gvc_ptr;				// Pointer to the GVC
	unsigned long int*		end_ptr;				// Pointer to the last VC
	unsigned long long		opt_clock;				// Optimal clock value for ET delivery

	unsigned long long		ticks;					// Total number of ticks occurred
	unsigned long long		ticks_standing;			// Number of ticks signed as standing
	unsigned long long		ticks_delivered;		// Number of ticks who have lead to CFV

	unsigned short int		sec_addr_defined;		// True if delivery addresses are provided
	unsigned long long		sec_start_addr;			// Beginning of delivery section
	unsigned long long		sec_end_addr;			// End of delivery section

	double					no_val_cost;			// Expected cost to pay without early validation
	unsigned long long		val_clock;				// Last validation clock (relative to the opt_clock)
	unsigned long long		et_clocks;				// Number of clocks between two ET events

	delivery_mode	mode;
} control_buffer;

#endif
