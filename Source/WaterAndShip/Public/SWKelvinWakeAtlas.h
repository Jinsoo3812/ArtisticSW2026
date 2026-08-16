#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Fixed Fr=0.50 Golden Image used identically by M7 CPU and GPU. */
class WATERANDSHIP_API FSWKelvinWakeAtlas
{
public:
	static FSWKelvinWakeAtlas& Get();
	bool Initialize();
	bool IsReady() const { return bReady; }
	float SampleFixedNormalized(float Downstream01, float LateralSigned01) const;
	UTexture2D* CreateTransientTexture(const FName& Name) const;

	static constexpr int32 ResolutionU = 512;
	static constexpr int32 ResolutionV = 256;
	static constexpr int32 TextureWidth = ResolutionV;
	static constexpr int32 TextureHeight = ResolutionU;

private:
	float ReadTexel(int32 UIndex, int32 VIndex) const;
	TArray<uint8> Payload;
	FCriticalSection InitializeLock;
	bool bReady = false;
};
