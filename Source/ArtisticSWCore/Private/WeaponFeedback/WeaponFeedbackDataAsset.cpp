#include "WeaponFeedback/WeaponFeedbackDataAsset.h"

#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

USoundBase* FWeaponSwingSoundSet::ChooseSound() const
{
	TArray<USoundBase*> ValidSounds;
	ValidSounds.Reserve(Sounds.Num());
	for (USoundBase* Sound : Sounds)
	{
		if (IsValid(Sound))
		{
			ValidSounds.Add(Sound);
		}
	}

	return ValidSounds.IsEmpty()
		? nullptr
		: ValidSounds[FMath::RandRange(0, ValidSounds.Num() - 1)];
}

float FWeaponSwingSoundSet::ChoosePitchMultiplier() const
{
	const float MinPitch = FMath::Max(0.01f, FMath::Min(PitchRange.X, PitchRange.Y));
	const float MaxPitch = FMath::Max(MinPitch, FMath::Max(PitchRange.X, PitchRange.Y));
	return FMath::FRandRange(MinPitch, MaxPitch);
}

bool FWeaponTrailFeedback::IsConfigured() const
{
	if (!IsValid(NiagaraSystem.Get()))
	{
		return false;
	}

	return !UsesEndpointParameters()
		|| (!StartPositionParameter.IsNone() && !EndPositionParameter.IsNone());
}

const FWeaponSwingSoundSet* UWeaponFeedbackDataAsset::FindSwingSoundSet(FName SoundSetName) const
{
	const FName RequestedName = SoundSetName.IsNone() ? FName(TEXT("Default")) : SoundSetName;
	const FWeaponSwingSoundSet* DefaultSet = nullptr;

	for (const FWeaponSwingSoundSet& SoundSet : SwingSoundSets)
	{
		if (SoundSet.SoundSetName == RequestedName)
		{
			return &SoundSet;
		}

		if (SoundSet.SoundSetName == TEXT("Default"))
		{
			DefaultSet = &SoundSet;
		}
	}

	return DefaultSet;
}
