#include "Animation/AN_EquipmentAttachItem.h"

#include "BasePlayer.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"

UAN_EquipmentAttachItem::UAN_EquipmentAttachItem()
{
	bIsNativeBranchingPoint = true;
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void UAN_EquipmentAttachItem::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (ABasePlayer* Player = Cast<ABasePlayer>(MeshComp->GetOwner()))
	{
		if (UPlayerEquipmentComponent* Equipment = Player->GetEquipmentComponent())
			Equipment->HandleEquipmentAttachNotify(Animation);
	}
}

FString UAN_EquipmentAttachItem::GetNotifyName_Implementation() const
{
	return TEXT("Equipment Attach Item");
}
