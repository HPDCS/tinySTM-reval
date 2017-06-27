#pragma once

#include "../datatypes/task.h"

int		StatsInit(int);
void	StatsFini(void);
void	StatsUpdate(task_t*);
void	StatsPrint(void);
void	EnableUpdate(void);
void	DisableUpdate(void);
int		IsEnableUpdate(void);