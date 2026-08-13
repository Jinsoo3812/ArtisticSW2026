#include "AnimNotify/AN_WeaponSwingSound.h"

#include "WeaponFeedback/WeaponFeedbackComponent.h"
#include "WeaponFeedback/WeaponFeedbackLibrary.h"

UAN_WeaponSwingSound::UAN_WeaponSwingSound()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(70, 170, 255, 255);
	bShouldFireInEditor = false;
#endif
}

void UAN_WeaponSwingSound::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (UWeaponFeedbackComponent* Feedback = UWeaponFeedbackLibrary::FindWeaponFeedbackComponent(MeshComp))
	{
		Feedback->PlaySwingSound(SoundSetName, VolumeMultiplier, PitchMultiplier);
	}
}

FString UAN_WeaponSwingSound::GetNotifyName_Implementation() const
{
	return SoundSetName.IsNone()
		? TEXT("Weapon Swing Sound")
		: FString::Printf(TEXT("Swing Sound: %s"), *SoundSetName.ToString());
}
