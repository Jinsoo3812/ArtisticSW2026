#include "AnimNotify/ANS_WeaponTrail.h"

#include "WeaponFeedback/WeaponFeedbackComponent.h"
#include "WeaponFeedback/WeaponFeedbackLibrary.h"

UANS_WeaponTrail::UANS_WeaponTrail()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(100, 255, 150, 255);
	bShouldFireInEditor = false;
#endif
}

void UANS_WeaponTrail::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	if (UWeaponFeedbackComponent* Feedback = UWeaponFeedbackLibrary::FindWeaponFeedbackComponent(MeshComp))
	{
		Feedback->BeginWeaponTrail();
	}
}

void UANS_WeaponTrail::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	if (UWeaponFeedbackComponent* Feedback = UWeaponFeedbackLibrary::FindWeaponFeedbackComponent(MeshComp))
	{
		Feedback->UpdateWeaponTrail();
	}
}

void UANS_WeaponTrail::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (UWeaponFeedbackComponent* Feedback = UWeaponFeedbackLibrary::FindWeaponFeedbackComponent(MeshComp))
	{
		Feedback->EndWeaponTrail();
	}
}

FString UANS_WeaponTrail::GetNotifyName_Implementation() const
{
	return TEXT("Weapon Trail");
}
