#pragma once

#include "CoreMinimal.h"
#include "SWShipWakeTypes.h"

class UTexture2D;

/** Multi-profile Golden Image atlas (Fr=0.30, 0.50, 0.70, 1.00) used identically by CPU and GPU. */
class WATERANDSHIP_API FSWKelvinWakeAtlas
{
public:
	static FSWKelvinWakeAtlas& Get();
	bool Initialize();
	bool IsReady() const { return bReady; }
	bool IsProfileReady(ESWKelvinFroudeProfile Profile) const;
	float SampleFixedNormalized(float Downstream01, float LateralSigned01,
		ESWKelvinFroudeProfile Profile = ESWKelvinFroudeProfile::Fr_0_50) const;
	UTexture2D* CreateTransientTexture(ESWKelvinFroudeProfile Profile, const FName& Name) const;

	static constexpr int32 ResolutionU = 512;
	static constexpr int32 ResolutionV = 256;
	static constexpr int32 TextureWidth = ResolutionV;
	static constexpr int32 TextureHeight = ResolutionU;
	static constexpr int32 ProfileCount = 4;

private:
	int32 ProfileToIndex(ESWKelvinFroudeProfile Profile) const;
	float ReadTexel(int32 ProfileIndex, int32 UIndex, int32 VIndex) const;
	TArray<uint8> Payloads[ProfileCount];
	bool ProfileReady[ProfileCount] = { false, false, false, false };
	FCriticalSection InitializeLock;
	bool bReady = false;
};
