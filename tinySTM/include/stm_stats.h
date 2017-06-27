/********************************************
 * SOFTWARE TRANSACTIONAL MEMORY STATISTICS *
 ********************************************/
#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "atomic.h"


#ifndef RDTSC
#define RDTSC() ({ \
	unsigned int cycles_low; \
	unsigned int cycles_high; \
	asm volatile ( \
		"RDTSC\n\t" \
		"mov %%edx, %0\n\t" \
		"mov %%eax, %1\n\t" \
		: \
		"=r" (cycles_high), "=r" (cycles_low) \
		: \
		: \
		"%rax", "%rdx" \
	); \
	(((uint64_t) cycles_high << 32) | cycles_low); \
})
#endif

static inline __attribute__((always_inline))
void stm_stats_entry_update(stm_stats_entry_t *entry, uint64_t next) {
	uint64_t prev;

	prev = entry->local;
	entry->local = next;

	// if (!prev) {
	// 	return;
	// }

	entry->samples += 1;

#ifdef STM_STATS_FAST
	entry->mean += next - prev;
#else
	double diff = next - prev;
	double delta = diff - entry->mean;
	entry->mean += (delta / entry->samples);
	entry->sqsum += delta * (diff - entry->mean);
#endif
}

static inline __attribute__((always_inline))
void stm_stats_entry_reset(stm_stats_entry_t *entry, uint64_t value) {
	entry->local = value;
}

static inline void stm_stats_entry_combine(stm_stats_entry_t *entry_a, stm_stats_entry_t *entry_b) {
	size_t samples_a, samples_b;
	double mean_a, mean_b;

	samples_a = entry_a->samples;
	samples_b = entry_b->samples;

	if (samples_a + samples_b == 0) {
		return;
	}

	mean_a = entry_a->mean;
	mean_b = entry_b->mean;

	// Final formulas must be:
	//
	//             sqsum_a + samples_a * mean_a^2 + sqsum_b + samples_b * mean_b^2
	// SQSUM    =  ---------------------------------------------------------------  -  MEAN
	//                                     SAMPLES
	//             samples_a * mean_a + samples_b * mean_b
	// MEAN     =  ---------------------------------------
	//                             SAMPLES
	// SAMPLES  =  samples_a + samples_b
	//
	// https://stats.stackexchange.com/questions/43159/how-to-calculate-pooled-variance-of-two-groups-given-known-group-variances-mean

#ifdef STM_STATS_FAST
	entry_a->samples = samples_a + samples_b;
	entry_a->mean = mean_a + mean_b;
	// FIXME: Compute variance
#else
	double samples_mean_a = samples_a * mean_a;
	double samples_mean_b = samples_b * mean_b;

	entry_a->sqsum += samples_mean_a * mean_a + entry_b->sqsum + samples_mean_b * mean_b;
	entry_a->mean = samples_mean_a + samples_mean_b;
	entry_a->samples = samples_a + samples_b;
	entry_a->mean /= entry_a->samples;
	entry_a->sqsum -= entry_a->mean * entry_a->samples;
#endif
}

static inline void stm_stats_combine(stm_stats_t *stats_a, stm_stats_t *stats_b) {
	stm_stats_entry_combine(&stats_a->completion, &stats_b->completion);
	stm_stats_entry_combine(&stats_a->commit, &stats_b->commit);
	stm_stats_entry_combine(&stats_a->read, &stats_b->read);
	stm_stats_entry_combine(&stats_a->write, &stats_b->write);
	stm_stats_entry_combine(&stats_a->extend_plt_real, &stats_b->extend_plt_real);
	stm_stats_entry_combine(&stats_a->extend_plt_fake, &stats_b->extend_plt_fake);
	stm_stats_entry_combine(&stats_a->extend_ea_real, &stats_b->extend_ea_real);
	stm_stats_entry_combine(&stats_a->extend_ea_fake, &stats_b->extend_ea_fake);
	stm_stats_entry_combine(&stats_a->abort_plt_real, &stats_b->abort_plt_real);
	stm_stats_entry_combine(&stats_a->abort_plt_fake, &stats_b->abort_plt_fake);
	stm_stats_entry_combine(&stats_a->abort_ea_real, &stats_b->abort_ea_real);
	stm_stats_entry_combine(&stats_a->abort_ea_fake, &stats_b->abort_ea_fake);
}

static inline void stm_stats_list_insert(stm_stats_list_t *list, stm_stats_t *stats) {
	stm_stats_node_t *node;

	node = malloc(sizeof(stm_stats_node_t));
	if (node == NULL) {
		perror("malloc stm stats node");
		exit(-1);
	}

	node->ptr = stats;
	do {
		node->next = list->head;
	} while (ATOMIC_CAS_FULL(&list->head, node->next, node) == 0);
}

static inline void stm_stats_list_combine(stm_stats_list_t *list, stm_stats_t *stats) {
	stm_stats_node_t *node;

	node = list->head;
	while (node) {
		if (node->ptr == NULL) {
			perror("stm stats list combine empty node");
			exit(-1);
		}

		stm_stats_combine(stats, node->ptr);

		free(node->ptr);
		node = node->next;
	}
}

static inline void stm_stats_entry_print(FILE *file, stm_stats_entry_t *entry) {
#ifdef STM_STATS_FAST
	fprintf(file, "%lu\t%f\t%f\n",
		entry->samples, entry->mean / entry->samples, 0.0);
#else
	fprintf(file, "%lu\t%f\t%f\n",
		entry->samples, entry->mean, sqrt(entry->sqsum / entry->samples));
#endif
}

#define STM_STATS_ENTRY_PRINT(stream, stats, label) \
	fprintf((stream), "[%s]\n", #label); \
	for (i = 0; i < nb_profiles; ++i) \
		stm_stats_entry_print((stream), &(&stats[i])->label); \
	fprintf((stream), "\n")
