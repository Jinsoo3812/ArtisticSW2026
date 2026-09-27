#include "Animation/AN_RollRecovery.h"

#include "AnimNotify/AN_SendGameplayEvent.h"
#include "BaseGameplayTags.h"

UAN_RollRecovery::UAN_RollRecovery()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(90, 220, 120, 255);
#endif
}

void UAN_RollRecovery::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	UAN_SendGameplayEvent::SendGameplayEventToMeshOwner(
		MeshComp,
		Event_Ability_Roll_Recovery);
}

FString UAN_RollRecovery::GetNotifyName_Implementation() const
{
	return TEXT("Roll Recovery");
}
