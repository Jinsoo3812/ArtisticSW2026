#include "Balance/FixedChestDropData.h"

float FFixedChestDropEntry::GetChance(EProgressionZone Zone) const
{
	for (const FFixedChestZoneChance& ZoneChance : ZoneChances)
	{
		if (ZoneChance.Zone == Zone)
		{
			return FMath::Clamp(ZoneChance.ChancePercent, 0.f, 100.f) / 100.f;
		}
	}
	return 0.f;
}
