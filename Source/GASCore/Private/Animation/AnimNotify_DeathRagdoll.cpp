#include "Animation/AnimNotify_DeathRagdoll.h"

#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UAnimNotify_DeathRagdoll::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UBaseHealthComponent* HealthComponent = Owner->FindComponentByClass<UBaseHealthComponent>())
	{
		HealthComponent->FinishDeath();
	}
}

FString UAnimNotify_DeathRagdoll::GetNotifyName_Implementation() const
{
	return TEXT("Death Ragdoll Point");
}
