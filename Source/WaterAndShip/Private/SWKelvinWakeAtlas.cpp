#include "SWKelvinWakeAtlas.h"

#include "Engine/Texture2D.h"
#include "Math/Float16.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWKelvinGolden, Log, All);

namespace
{
	constexpr int64 ChannelsPerTexel = 4;
	constexpr int64 GoldenBytes = static_cast<int64>(FSWKelvinWakeAtlas::ResolutionU)
		* FSWKelvinWakeAtlas::ResolutionV * ChannelsPerTexel * sizeof(uint16);

	const TCHAR* ProfileFileNames[FSWKelvinWakeAtlas::ProfileCount] = {
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr030_fp16.bin"),
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr050_fp16.bin"),
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr070_fp16.bin"),
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr100_fp16.bin")
	};
}

FSWKelvinWakeAtlas& FSWKelvinWakeAtlas::Get()
{
	static FSWKelvinWakeAtlas Golden;
	return Golden;
}

int32 FSWKelvinWakeAtlas::ProfileToIndex(const ESWKelvinFroudeProfile Profile) const
{
	switch (Profile)
	{
	case ESWKelvinFroudeProfile::Fr_0_30: return 0;
	case ESWKelvinFroudeProfile::Fr_0_50: return 1;
	case ESWKelvinFroudeProfile::Fr_0_70: return 2;
	case ESWKelvinFroudeProfile::Fr_1_00: return 3;
	default: return 1;
	}
}

bool FSWKelvinWakeAtlas::Initialize()
{
	if (bReady)
	{
		return true;
	}
	FScopeLock Lock(&InitializeLock);
	if (bReady)
	{
		return true;
	}

	int32 LoadedCount = 0;
	for (int32 Index = 0; Index < ProfileCount; ++Index)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), ProfileFileNames[Index]);
		if (!FFileHelper::LoadFileToArray(Payloads[Index], *Path) || Payloads[Index].Num() != GoldenBytes)
		{
			UE_LOG(LogSWKelvinGolden, Error, TEXT("M7 Golden Image load failed for profile %d: %s (%d/%lld bytes)"),
				Index, *Path, Payloads[Index].Num(), GoldenBytes);
			Payloads[Index].Reset();
			ProfileReady[Index] = false;
		}
		else
		{
			ProfileReady[Index] = true;
			++LoadedCount;
		}
	}

	bReady = (LoadedCount > 0);
	UE_LOG(LogSWKelvinGolden, Display,
		TEXT("M7 Multi-Profile Golden Images loaded: %d/%d profiles ready (%dx%d RGBA16F Normalized)"),
		LoadedCount, ProfileCount, TextureWidth, TextureHeight);
	return bReady;
}

bool FSWKelvinWakeAtlas::IsProfileReady(const ESWKelvinFroudeProfile Profile) const
{
	const int32 Index = ProfileToIndex(Profile);
	return Index >= 0 && Index < ProfileCount && ProfileReady[Index];
}

float FSWKelvinWakeAtlas::ReadTexel(const int32 ProfileIndex, const int32 UIndex, const int32 VIndex) const
{
	if (ProfileIndex < 0 || ProfileIndex >= ProfileCount || !ProfileReady[ProfileIndex])
	{
		return 0.0f;
	}
	// Stride across 4 channels (R=Height, G=GradU, B=GradV, A=Mask); Channel 0 (R) is at offset 0
	const int64 ByteIndex = (static_cast<int64>(UIndex) * ResolutionV + VIndex) * ChannelsPerTexel * sizeof(uint16);
	if (!Payloads[ProfileIndex].IsValidIndex(ByteIndex + 1))
	{
		return 0.0f;
	}
	FFloat16 Half;
	FMemory::Memcpy(&Half.Encoded, Payloads[ProfileIndex].GetData() + ByteIndex, sizeof(uint16));
	// Payloads are pre-normalized in [-1.0, 1.0] by peak amplitude during bake
	return Half.GetFloat();
}

float FSWKelvinWakeAtlas::SampleFixedNormalized(
	const float Downstream01,
	const float LateralSigned01,
	const ESWKelvinFroudeProfile Profile) const
{
	const int32 ProfileIndex = ProfileToIndex(Profile);
	if (!IsProfileReady(Profile) || Downstream01 < 0.0f || Downstream01 > 1.0f
		|| FMath::Abs(LateralSigned01) > 1.0f)
	{
		return 0.0f;
	}
	const float U = Downstream01 * (ResolutionU - 1);
	const float V = (LateralSigned01 * 0.5f + 0.5f) * (ResolutionV - 1);
	const int32 U0 = FMath::FloorToInt(U);
	const int32 V0 = FMath::FloorToInt(V);
	const int32 U1 = FMath::Min(U0 + 1, ResolutionU - 1);
	const int32 V1 = FMath::Min(V0 + 1, ResolutionV - 1);
	const float A = FMath::Lerp(ReadTexel(ProfileIndex, U0, V0), ReadTexel(ProfileIndex, U0, V1), V - V0);
	const float B = FMath::Lerp(ReadTexel(ProfileIndex, U1, V0), ReadTexel(ProfileIndex, U1, V1), V - V0);
	return FMath::Lerp(A, B, U - U0);
}

UTexture2D* FSWKelvinWakeAtlas::CreateTransientTexture(const ESWKelvinFroudeProfile Profile, const FName& Name) const
{
	const int32 Index = ProfileToIndex(Profile);
	if (!IsProfileReady(Profile))
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PF_FloatRGBA, Name,
		TConstArrayView64<uint8>(Payloads[Index].GetData(), Payloads[Index].Num()));
	if (Texture)
	{
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_HDR;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
	}
	return Texture;
}
