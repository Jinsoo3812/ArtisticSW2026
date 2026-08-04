#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_WeaponSwingSound.generated.h"

/** Plays the equipped weapon's configured swing sound set. */
UCLASS(meta = (DisplayName = "Weapon Swing Sound"))
class ARTISTICSWCORE_API UAN_WeaponSwingSound : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAN_WeaponSwingSound();

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sound")
	FName SoundSetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sound", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Sound", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.0f;
};
