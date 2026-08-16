#include "SWKelvinWakeAtlas.h"

#include "Engine/Texture2D.h"
#include "HAL/PlatformFileManager.h"
#include "Math/Float16.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWKelvinAtlas, Log, All);

namespace SWKelvinAtlasData
{
	constexpr float UMin = 0.0f;
	constexpr float UMax = 10.0f;
	constexpr float VMin = -3.0f;
	constexpr float VMax = 3.0f;
	constexpr float MinimumNormalizablePeak = 1.0e-3f;
	constexpr int64 ExpectedPayloadBytes =
		static_cast<int64>(FSWKelvinWakeAtlas::NumSlices)
		* FSWKelvinWakeAtlas::ResolutionU
		* FSWKelvinWakeAtlas::ResolutionV
		* sizeof(uint16);

	constexpr float FroudeSlices[FSWKelvinWakeAtlas::NumSlices] = {
		0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f,
		0.80f, 1.00f, 1.25f, 1.50f, 1.75f, 2.00f
	};
	constexpr float SliceMax[FSWKelvinWakeAtlas::NumSlices] = {
		0.0000000420757f, 0.0315173446f, 0.4836892680f, 1.3871971383f,
		2.5378550849f, 3.9109640445f, 5.5341061850f, 9.6514712048f,
		16.0166310274f, 21.9746402757f, 27.1834687593f, 35.4332880637f
	};
}

FSWKelvinWakeAtlas& FSWKelvinWakeAtlas::Get()
{
	static FSWKelvinWakeAtlas Atlas;
	return Atlas;
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

	const FString AtlasPath = FPaths::Combine(
		FPaths::ProjectContentDir(),
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_atlas_fp16.bin"));
	TArray<uint8> LoadedPayload;
	if (!FFileHelper::LoadFileToArray(LoadedPayload, *AtlasPath)
		|| LoadedPayload.Num() != SWKelvinAtlasData::ExpectedPayloadBytes)
	{
		UE_LOG(LogSWKelvinAtlas, Error,
			TEXT("M4 Kelvin atlas load failed or has the wrong size: %s (got %d, expected %lld)"),
			*AtlasPath, LoadedPayload.Num(), SWKelvinAtlasData::ExpectedPayloadBytes);
		return false;
	}

	Payload = MoveTemp(LoadedPayload);
	bReady = true;
	UE_LOG(LogSWKelvinAtlas, Display,
		TEXT("M4 Kelvin atlas loaded: %dx%d packed R16F, 12 slices, SHA256 7b85a7f7..."),
		TextureWidth, TextureHeight);
	return true;
}

float FSWKelvinWakeAtlas::ReadTexel(const int32 SliceIndex, const int32 UIndex, const int32 VIndex) const
{
	const int64 TexelIndex =
		(static_cast<int64>(SliceIndex) * ResolutionU + UIndex) * ResolutionV + VIndex;
	const int64 ByteIndex = TexelIndex * sizeof(uint16);
	if (!bReady || !Payload.IsValidIndex(ByteIndex + 1))
	{
		return 0.0f;
	}

	FFloat16 Half;
	FMemory::Memcpy(&Half.Encoded, Payload.GetData() + ByteIndex, sizeof(uint16));
	return Half.GetFloat();
}

float FSWKelvinWakeAtlas::SampleSlice(
	const int32 SliceIndex,
	const float DownstreamLambda,
	const float LateralLambda) const
{
	if (DownstreamLambda < SWKelvinAtlasData::UMin || DownstreamLambda > SWKelvinAtlasData::UMax
		|| LateralLambda < SWKelvinAtlasData::VMin || LateralLambda > SWKelvinAtlasData::VMax)
	{
		return 0.0f;
	}

	const float UTexel = (DownstreamLambda - SWKelvinAtlasData::UMin)
		/ (SWKelvinAtlasData::UMax - SWKelvinAtlasData::UMin) * (ResolutionU - 1);
	const float VTexel = (LateralLambda - SWKelvinAtlasData::VMin)
		/ (SWKelvinAtlasData::VMax - SWKelvinAtlasData::VMin) * (ResolutionV - 1);
	const int32 U0 = FMath::Clamp(FMath::FloorToInt(UTexel), 0, ResolutionU - 1);
	const int32 V0 = FMath::Clamp(FMath::FloorToInt(VTexel), 0, ResolutionV - 1);
	const int32 U1 = FMath::Min(U0 + 1, ResolutionU - 1);
	const int32 V1 = FMath::Min(V0 + 1, ResolutionV - 1);
	const float UAlpha = UTexel - U0;
	const float VAlpha = VTexel - V0;

	const float A = FMath::Lerp(ReadTexel(SliceIndex, U0, V0), ReadTexel(SliceIndex, U0, V1), VAlpha);
	const float B = FMath::Lerp(ReadTexel(SliceIndex, U1, V0), ReadTexel(SliceIndex, U1, V1), VAlpha);
	return FMath::Lerp(A, B, UAlpha);
}

float FSWKelvinWakeAtlas::SampleNormalized(
	const float DownstreamLambda,
	const float LateralLambda,
	const float Froude) const
{
	if (!bReady || Froude <= SWKelvinAtlasData::FroudeSlices[0])
	{
		return 0.0f;
	}

	int32 Upper = 1;
	while (Upper < NumSlices && Froude > SWKelvinAtlasData::FroudeSlices[Upper])
	{
		++Upper;
	}
	Upper = FMath::Min(Upper, NumSlices - 1);
	const int32 Lower = Upper - 1;
	const float Alpha = FMath::Clamp(
		(Froude - SWKelvinAtlasData::FroudeSlices[Lower])
		/ (SWKelvinAtlasData::FroudeSlices[Upper] - SWKelvinAtlasData::FroudeSlices[Lower]),
		0.0f, 1.0f);

	const float LowerPeak = SWKelvinAtlasData::SliceMax[Lower];
	const float UpperPeak = SWKelvinAtlasData::SliceMax[Upper];
	const float LowerValue = LowerPeak >= SWKelvinAtlasData::MinimumNormalizablePeak
		? SampleSlice(Lower, DownstreamLambda, LateralLambda) / LowerPeak
		: 0.0f;
	const float UpperValue = UpperPeak >= SWKelvinAtlasData::MinimumNormalizablePeak
		? SampleSlice(Upper, DownstreamLambda, LateralLambda) / UpperPeak
		: 0.0f;
	const float StartupFade = FMath::SmoothStep(0.20f, 0.30f, Froude);
	return FMath::Lerp(LowerValue, UpperValue, Alpha) * StartupFade;
}

UTexture2D* FSWKelvinWakeAtlas::CreateTransientTexture(const FName& Name) const
{
	if (!bReady)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		TextureWidth,
		TextureHeight,
		PF_R16F,
		Name,
		TConstArrayView64<uint8>(Payload.GetData(), Payload.Num()));
	if (Texture)
	{
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_HDR;
		Texture->Filter = TF_Nearest;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
	}
	return Texture;
}
