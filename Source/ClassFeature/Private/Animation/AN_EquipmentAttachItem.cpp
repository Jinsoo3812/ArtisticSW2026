#include "Animation/AN_EquipmentAttachItem.h"

#include "BasePlayer.h"
#include "Components/SkeletalMeshComponent.h"

void UAN_EquipmentAttachItem::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (ABasePlayer* Player = Cast<ABasePlayer>(MeshComp->GetOwner()))
	{
		Player->HandleEquipmentAttachNotify();
	}
}

FString UAN_EquipmentAttachItem::GetNotifyName_Implementation() const
{
	return TEXT("Equipment Attach Item");
}
