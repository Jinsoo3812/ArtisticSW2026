#include "SWCabinWaterCullData.h"
#include "Engine/VolumeTexture.h"

void USWCabinWaterCullData::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITORONLY_DATA
	// Migrate existing baked assets on load, including cook. Packaged builds
	// use the serialized CPU copy without accessing GPU or editor source data.
	if (MaskTexture)
	{
		MaskTexture->ConditionalPostLoad();
		TArray64<uint8> Pixels;
		if (MaskTexture->Source.GetFormat() == TSF_G8
			&& MaskTexture->Source.GetSizeX() == Resolution.X
			&& MaskTexture->Source.GetSizeY() == Resolution.Y
			&& MaskTexture->Source.GetNumSlices() == Resolution.Z
			&& MaskTexture->Source.GetMipData(Pixels, 0)
			&& Pixels.Num() == int64(Resolution.X) * Resolution.Y * Resolution.Z
			&& Pixels.Num() <= MAX_int32)
		{
			OccupancyVoxels.Reset();
			OccupancyVoxels.Append(Pixels.GetData(), int32(Pixels.Num()));
		}
	}
#endif
}

bool USWCabinWaterCullData::ContainsLocalPosition(const FVector& Position, float Threshold) const
{
	const FVector Extent = LocalBoundsMax - LocalBoundsMin;
	if (Position.ContainsNaN() || Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0
		|| Extent.X <= 0 || Extent.Y <= 0 || Extent.Z <= 0
		|| OccupancyVoxels.Num() != int64(Resolution.X) * Resolution.Y * Resolution.Z)
	{
		return false;
	}
	const FVector UVW = (Position - LocalBoundsMin) / Extent;
	if (UVW.X < 0 || UVW.Y < 0 || UVW.Z < 0 || UVW.X > 1 || UVW.Y > 1 || UVW.Z > 1)
	{
		return false;
	}
	const FVector Texel = UVW * FVector(Resolution) - FVector(0.5);
	const FIntVector Base(FMath::FloorToInt(Texel.X), FMath::FloorToInt(Texel.Y), FMath::FloorToInt(Texel.Z));
	const FVector Alpha = Texel - FVector(Base);
	double Occupancy = 0;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				const int32 IX = FMath::Clamp(Base.X + X, 0, Resolution.X - 1);
				const int32 IY = FMath::Clamp(Base.Y + Y, 0, Resolution.Y - 1);
				const int32 IZ = FMath::Clamp(Base.Z + Z, 0, Resolution.Z - 1);
				Occupancy += OccupancyVoxels[IX + Resolution.X * (IY + Resolution.Y * IZ)] / 255.0
					* (X ? Alpha.X : 1 - Alpha.X)
					* (Y ? Alpha.Y : 1 - Alpha.Y)
					* (Z ? Alpha.Z : 1 - Alpha.Z);
			}
		}
	}
	return Occupancy >= Threshold;
}
