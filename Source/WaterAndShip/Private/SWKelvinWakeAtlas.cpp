#include "SWKelvinWakeAtlas.h"

#include "Engine/Texture2D.h"
#include "Math/Float16.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWKelvinGolden, Log, All);

namespace
{
	constexpr float FixedPeak = 1.3871971383f;
	constexpr int64 GoldenBytes = static_cast<int64>(FSWKelvinWakeAtlas::ResolutionU)
		* FSWKelvinWakeAtlas::ResolutionV * sizeof(uint16);
}

FSWKelvinWakeAtlas& FSWKelvinWakeAtlas::Get()
{
	static FSWKelvinWakeAtlas Golden;
	return Golden;
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

	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(),
		TEXT("New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr050_fp16.bin"));
	if (!FFileHelper::LoadFileToArray(Payload, *Path) || Payload.Num() != GoldenBytes)
	{
		UE_LOG(LogSWKelvinGolden, Error, TEXT("M7 Golden Image load failed: %s (%d/%lld bytes)"),
			*Path, Payload.Num(), GoldenBytes);
		Payload.Reset();
		return false;
	}
	bReady = true;
	UE_LOG(LogSWKelvinGolden, Display,
		TEXT("M7 fixed Golden Image loaded: Fr=0.50, %dx%d R16F"), TextureWidth, TextureHeight);
	return true;
}

float FSWKelvinWakeAtlas::ReadTexel(const int32 UIndex, const int32 VIndex) const
{
	const int64 ByteIndex = (static_cast<int64>(UIndex) * ResolutionV + VIndex) * sizeof(uint16);
	if (!bReady || !Payload.IsValidIndex(ByteIndex + 1))
	{
		return 0.0f;
	}
	FFloat16 Half;
	FMemory::Memcpy(&Half.Encoded, Payload.GetData() + ByteIndex, sizeof(uint16));
	return Half.GetFloat() / FixedPeak;
}

float FSWKelvinWakeAtlas::SampleFixedNormalized(
	const float Downstream01,
	const float LateralSigned01) const
{
	if (!bReady || Downstream01 < 0.0f || Downstream01 > 1.0f
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
	const float A = FMath::Lerp(ReadTexel(U0, V0), ReadTexel(U0, V1), V - V0);
	const float B = FMath::Lerp(ReadTexel(U1, V0), ReadTexel(U1, V1), V - V0);
	return FMath::Lerp(A, B, U - U0);
}

UTexture2D* FSWKelvinWakeAtlas::CreateTransientTexture(const FName& Name) const
{
	if (!bReady)
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PF_R16F, Name,
		TConstArrayView64<uint8>(Payload.GetData(), Payload.Num()));
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
