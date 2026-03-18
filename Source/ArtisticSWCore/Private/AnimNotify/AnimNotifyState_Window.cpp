
#include "AnimNotify/AnimNotifyState_Window.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UAnimNotifyState_Window::UAnimNotifyState_Window()
{
	Name = TEXT("Window");
}

FString UAnimNotifyState_Window::GetNotifyName_Implementation() const
{
	return Name;
}

void UAnimNotifyState_Window::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference
)
{
	if (!MeshComp)
		return;

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
		return;
	
	if (BeginEventTag.IsValid())
	{
		FGameplayEventData EventData;
		EventData.EventTag = BeginEventTag;
		EventData.Instigator = OwnerActor;
		EventData.Target = OwnerActor;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			OwnerActor,
			BeginEventTag,
			EventData
		);
	}

}

void UAnimNotifyState_Window::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference
)
{
	if (!MeshComp)
		return;

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
		return;

	if (EndEventTag.IsValid())
	{
		FGameplayEventData EventData;
		EventData.EventTag = EndEventTag;
		EventData.Instigator = OwnerActor;
		EventData.Target = OwnerActor;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			OwnerActor,
			EndEventTag,
			EventData
		);
	}
}