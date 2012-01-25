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
	int percent;	/* The battery's percentage (0-100) */
	short features; /* a bit array that specifies features of the battery */
};

/**
 * Bit features: 
 * 1 - Battery is not present
 * 2 - Battery uses energy instead of charge (in /sys)
 * 4 - Battery doesn't describe rate of discharge, client must determine discharge rate
 * 
 * Therefore
 * 0110 means that the battery is present, and uses energy instead of charge, and doesn't desribe rate.
 **/


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


