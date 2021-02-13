#ifndef MCLI_SERVICE_H
#define MCLI_SERVICE_H

#define MAX_TUNERS_IN_MENU 16

typedef struct
{
	int type[MAX_TUNERS_IN_MENU];
	char name[MAX_TUNERS_IN_MENU][128];
    int preference[MAX_TUNERS_IN_MENU];
} mclituner_info_t;

#endif
