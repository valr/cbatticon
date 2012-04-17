#include "libubat.h"


int main()
{
	struct BatteryInfo *info = (struct BatteryInfo *)init_battery("BAT0");
	if (info == NULL) return 0;
	printf("%d/%d - %d\n", info->capacity, info->max, info->status);
	printf("%d\n", info->features);	
	if (info->features & 2) printf("Battery uses Energy instead of charge\n");
	
	if (info->features & 4) printf("Battery doesn't support rate\n");
	
	printf("%d%%\n", (int)((float)info->capacity / (float)info->max * 100.0));
} 
