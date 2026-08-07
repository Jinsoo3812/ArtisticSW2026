#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponFeedbackDataAsset.generated.h"

class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;

UENUM(BlueprintType)
enum class EWeaponTrailPlacementMode : uint8
{
	/** Spawn the authored Niagara system on the weapon root/socket and let the system generate its own trail. */
	AttachedSystem,

	/** Update two Niagara Vector user parameters from the weapon's blade endpoints every notify tick. */
	EndpointParameters
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FWeaponSwingSoundSet
{
	GENERATED_BODY()

	/** Name selected by AN_WeaponSwingSound. Use Default when no special variant is needed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	FName SoundSetName = TEXT("Default");

	/** One entry is chosen randomly each time the notify fires. Sound Cues and MetaSounds are supported. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	/** Random pitch range applied after the notify's Pitch Multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	FVector2D PitchRange = FVector2D(0.96f, 1.04f);

	/** Optional override. The Sound asset's attenuation is used when this is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundAttenuation> AttenuationSettings;

	/** Optional override used to limit rapid combo/overlap playback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundConcurrency> ConcurrencySettings;

	USoundBase* ChooseSound() const;
	float ChoosePitchMultiplier() const;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FWeaponTrailFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	TObjectPtr<UNiagaraSystem> NiagaraSystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	EWeaponTrailPlacementMode PlacementMode = EWeaponTrailPlacementMode::AttachedSystem;

	/** Optional weapon-mesh socket for Attached System mode. None attaches to the weapon root. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail", meta = (EditCondition = "PlacementMode == EWeaponTrailPlacementMode::AttachedSystem"))
	FName AttachSocketName = NAME_None;

	/** Fallback sockets used when the weapon actor does not supply endpoint components. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail|Endpoints", meta = (EditCondition = "PlacementMode == EWeaponTrailPlacementMode::EndpointParameters"))
	FName StartSocketName = TEXT("TrailStart");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail|Endpoints", meta = (EditCondition = "PlacementMode == EWeaponTrailPlacementMode::EndpointParameters"))
	FName EndSocketName = TEXT("TrailEnd");

	/** Niagara Vector user parameters. The supplied system should consume these in world space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail|Parameters", meta = (EditCondition = "PlacementMode == EWeaponTrailPlacementMode::EndpointParameters"))
	FName StartPositionParameter = TEXT("User.TrailStart");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail|Parameters", meta = (EditCondition = "PlacementMode == EWeaponTrailPlacementMode::EndpointParameters"))
	FName EndPositionParameter = TEXT("User.TrailEnd");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FVector ComponentScale = FVector::OneVector;

	/** Relative adjustment from the weapon root/socket, mainly for third-party attached systems. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	bool IsConfigured() const;
	bool UsesEndpointParameters() const { return PlacementMode == EWeaponTrailPlacementMode::EndpointParameters; }
};

/** Shared cosmetic feedback authored once per weapon family or individual weapon. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UWeaponFeedbackDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	const FWeaponSwingSoundSet* FindSwingSoundSet(FName SoundSetName) const;
	const FWeaponTrailFeedback& GetTrailFeedback() const { return Trail; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Feedback|Sound")
	TArray<FWeaponSwingSoundSet> SwingSoundSets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Feedback|Trail")
	FWeaponTrailFeedback Trail;
};
