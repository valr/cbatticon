#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * A structure describing the battery
 **/
struct BatteryInfo {
	char battery[8]; /* The battery's name */
	int status;		/* The battery's state */
	int capacity;		/* Capacity of the battery */
	int rate;		/* The rate of dischage */
	int max;	/* The max capacity (100%) */
};

/**
 * Enumeration for the battery status
 **/
enum {
	UNKNOWN = 0,
	MISSING,
	CHARGING,
	DISCHARGING,
	NOT_CHARGING,
	CHARGED,
	LOW_POWER
};


