#include "AN_SendGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"

UAN_SendGameplayEvent::UAN_SendGameplayEvent()
{
#if WITH_EDITORONLY_DATA
	// 에디터 타임라인에서 눈에 띄게 주황색으로 표시
	NotifyColor = FColor(255, 128, 0, 255);
#endif
}

bool UAN_SendGameplayEvent::SendGameplayEventToMeshOwner(
	USkeletalMeshComponent* MeshComp,
	FGameplayTag GameplayEventTag,
	float EventMagnitude)
{
	if (!MeshComp || !GameplayEventTag.IsValid())
	{
		return false;
	}

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	FGameplayEventData Payload;
	Payload.Instigator = OwnerActor;
	Payload.Target = OwnerActor;
	Payload.EventTag = GameplayEventTag;
	Payload.EventMagnitude = EventMagnitude;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, GameplayEventTag, Payload);
	return true;
}

void UAN_SendGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	SendGameplayEventToMeshOwner(MeshComp, EventTag);
}

FString UAN_SendGameplayEvent::GetNotifyName_Implementation() const
{
	// 에디터의 노티파이 트랙에 "AN_SendGameplayEvent" 대신 태그 이름이 출력되게 만듦
	if (EventTag.IsValid())
	{
		return EventTag.ToString();
	}
	return Super::GetNotifyName_Implementation();
}
