#include "WeaponFeedback/WeaponFeedbackLibrary.h"

#include "Components/SkeletalMeshComponent.h"
#include "WeaponFeedback/WeaponFeedbackComponent.h"

UWeaponFeedbackComponent* UWeaponFeedbackLibrary::FindWeaponFeedbackComponent(USkeletalMeshComponent* MeshComponent)
{
	AActor* MeshOwner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
	if (!MeshOwner)
	{
		return nullptr;
	}

	if (UWeaponFeedbackComponent* OwnerFeedback = MeshOwner->FindComponentByClass<UWeaponFeedbackComponent>())
	{
		if (OwnerFeedback->HasFeedbackData())
		{
			return OwnerFeedback;
		}
	}

	TArray<AActor*> AttachedActors;
	MeshOwner->GetAttachedActors(AttachedActors, true, false);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (!IsValid(AttachedActor) || AttachedActor->IsHidden())
		{
			continue;
		}

		UWeaponFeedbackComponent* Feedback = AttachedActor->FindComponentByClass<UWeaponFeedbackComponent>();
		if (Feedback && Feedback->HasFeedbackData())
		{
			return Feedback;
		}
	}

	return nullptr;
}
