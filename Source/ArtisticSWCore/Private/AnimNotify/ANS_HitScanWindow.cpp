#include "AnimNotify/ANS_HitScanWindow.h"

#include "AnimNotify/AN_SendGameplayEvent.h"
#include "BaseGameplayTags.h"

UANS_HitScanWindow::UANS_HitScanWindow()
{
	// Gameplay-critical montage windows must be evaluated as branching points.
	bIsNativeBranchingPoint = true;

#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 80, 40, 255);
	bShouldFireInEditor = false;
#endif
}

void UANS_HitScanWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	// Do not call Super: this native state owns dispatch even when an existing
	// Blueprint notify state is reparented to it. That prevents duplicate events.
	UAN_SendGameplayEvent::SendGameplayEventToMeshOwner(MeshComp, Event_HandleScan_Start, TotalDuration);
}

void UANS_HitScanWindow::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	// See NotifyBegin regarding intentionally bypassing Blueprint Received events.
	UAN_SendGameplayEvent::SendGameplayEventToMeshOwner(MeshComp, Event_HandleScan_Tick, FrameDeltaTime);
}

void UANS_HitScanWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// See NotifyBegin regarding intentionally bypassing Blueprint Received events.
	UAN_SendGameplayEvent::SendGameplayEventToMeshOwner(MeshComp, Event_HandleScan_End);
}

FString UANS_HitScanWindow::GetNotifyName_Implementation() const
{
	return TEXT("Hit Scan Window");
}
