#include "libubat.h"


int main()
{
	struct BatteryInfo *info = get_battery("BAT0");
	printf("%d/%d - %d\n", info->capacity, info->max, info->status);
} 
