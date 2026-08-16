#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Immutable FP16 payload shared by the M4 CPU sampler and runtime GPU atlas. */
class WATERANDSHIP_API FSWKelvinWakeAtlas
{
public:
	static FSWKelvinWakeAtlas& Get();

	bool Initialize();
	bool IsReady() const { return bReady; }
	float SampleNormalized(float DownstreamLambda, float LateralLambda, float Froude) const;
	UTexture2D* CreateTransientTexture(const FName& Name) const;

	static constexpr int32 NumSlices = 12;
	static constexpr int32 ResolutionU = 512;
	static constexpr int32 ResolutionV = 256;
	static constexpr int32 TextureWidth = ResolutionV;
	static constexpr int32 TextureHeight = ResolutionU * NumSlices;

private:
	float SampleSlice(int32 SliceIndex, float DownstreamLambda, float LateralLambda) const;
	float ReadTexel(int32 SliceIndex, int32 UIndex, int32 VIndex) const;

	TArray<uint8> Payload;
	FCriticalSection InitializeLock;
	bool bReady = false;
};
