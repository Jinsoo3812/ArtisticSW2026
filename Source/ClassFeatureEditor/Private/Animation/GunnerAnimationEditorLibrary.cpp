#include "Animation/GunnerAnimationEditorLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"

namespace GunnerAnimationAuthoring
{
	const FName SlotName(TEXT("GunnerFullBody"));
	UAnimGraphNode_Root* FindRoot(UAnimBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
			if (Graph->GetFName() == TEXT("AnimGraph"))
				for (UEdGraphNode* Node : Graph->Nodes)
					if (auto* Root = Cast<UAnimGraphNode_Root>(Node)) return Root;
		return nullptr;
	}
}

bool UGunnerAnimationEditorLibrary::ConfigureCombatMontage(UAnimMontage* Montage, UAnimSequence* Aim, UAnimSequence* Fire, float FireDuration)
{
	using namespace GunnerAnimationAuthoring;
	if (!Montage || !Aim || !Fire || Aim->IsValidAdditive() || Fire->IsValidAdditive()
		|| Aim->GetSkeleton() != Fire->GetSkeleton()) return false;
	Montage->Modify();
	Montage->SetSkeleton(Aim->GetSkeleton());
	Montage->SlotAnimTracks.Reset();
	auto& Track = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Track.SlotName = SlotName;
	float Start = 0.f;
	for (UAnimSequence* Sequence : {Aim, Fire, Aim})
	{
		FAnimSegment Segment;
		Segment.SetAnimReference(Sequence, true);
		Segment.StartPos = Start;
		Segment.AnimStartTime = 0.f;
		Segment.AnimEndTime = Sequence->GetPlayLength();
		Segment.AnimPlayRate = Sequence == Fire && FireDuration > 0.f ? Fire->GetPlayLength() / FireDuration : 1.f;
		Segment.LoopingCount = 1;
		Track.AnimTrack.AnimSegments.Add(Segment);
		Start += Sequence->GetPlayLength() / Segment.AnimPlayRate;
	}
	Montage->CompositeSections.Reset();
	// A newly authored montage has no data model to recalculate its length from yet.
	FFloatProperty* Length = FindFProperty<FFloatProperty>(UAnimSequenceBase::StaticClass(), TEXT("SequenceLength"));
	if (!Length) return false;
	Length->SetPropertyValue_InContainer(Montage, Start);
	Montage->AddAnimCompositeSection(TEXT("Aim"), 0.f);
	Montage->AddAnimCompositeSection(TEXT("Fire"), Aim->GetPlayLength());
	Montage->AddAnimCompositeSection(TEXT("Recover"), Aim->GetPlayLength() + (FireDuration > 0.f ? FireDuration : Fire->GetPlayLength()));
	Montage->CompositeSections[0].NextSectionName = TEXT("Aim");
	Montage->CompositeSections[1].NextSectionName = TEXT("Recover");
	Montage->CompositeSections[2].NextSectionName = TEXT("Recover");
	Montage->BlendIn.SetBlendTime(0.12f);
	Montage->BlendOut.SetBlendTime(0.15f);
	Montage->Notifies.Reset(); // Gameplay timing is owned by the gun GA.
	Montage->UpdateLinkableElements();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return FMath::IsNearlyEqual(Montage->GetPlayLength(), Start);
}

bool UGunnerAnimationEditorLibrary::ConnectCombatSlot(UAnimBlueprint* Blueprint)
{
	using namespace GunnerAnimationAuthoring;
	if (!Blueprint || !Blueprint->TargetSkeleton) return false;
	auto* Root = FindRoot(Blueprint);
	auto* Result = Root ? Root->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
	if (!Result || Result->LinkedTo.Num() != 1) return false;
	UEdGraph* Graph = Root->GetGraph();
	auto* Slot = Cast<UAnimGraphNode_Slot>(Result->LinkedTo[0]->GetOwningNode());
	if (!Slot || Slot->Node.SlotName != SlotName)
	{
		UEdGraphPin* Source = Result->LinkedTo[0];
		Slot = NewObject<UAnimGraphNode_Slot>(Graph, NAME_None, RF_Transactional);
		Slot->Node.SlotName = SlotName;
		Graph->AddNode(Slot, true, false);
		Slot->CreateNewGuid();
		Slot->AllocateDefaultPins();
		Slot->NodePosX = Root->NodePosX - 240;
		Slot->NodePosY = Root->NodePosY;
		Result->BreakAllPinLinks();
		if (!Graph->GetSchema()->TryCreateConnection(Source, Slot->FindPin(TEXT("Source"), EGPD_Input))
			|| !Graph->GetSchema()->TryCreateConnection(Slot->FindPin(TEXT("Pose"), EGPD_Output), Result)) return false;
	}
	Blueprint->TargetSkeleton->RegisterSlotNode(SlotName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
	return Blueprint->Status != BS_Error;
}

bool UGunnerAnimationEditorLibrary::AuditCombatAssets(UAnimBlueprint* Blueprint, UAnimMontage* Montage)
{
	using namespace GunnerAnimationAuthoring;
	if (!Blueprint || !Montage || Blueprint->Status == BS_Error || Blueprint->TargetSkeleton != Montage->GetSkeleton()
		|| Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].SlotName != SlotName
		|| Montage->CompositeSections.Num() != 3) return false;
	auto* Root = FindRoot(Blueprint);
	auto* Result = Root ? Root->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
	const auto* Slot = Result && Result->LinkedTo.Num() == 1 ? Cast<UAnimGraphNode_Slot>(Result->LinkedTo[0]->GetOwningNode()) : nullptr;
	if (!Slot || Slot->Node.SlotName != SlotName || Slot->FindPin(TEXT("Source"), EGPD_Input)->LinkedTo.Num() != 1) return false;
	for (const FName Section : {FName(TEXT("Aim")), FName(TEXT("Fire")), FName(TEXT("Recover"))})
		if (Montage->GetSectionIndex(Section) == INDEX_NONE) return false;
	for (const FAnimSegment& Segment : Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
		if (!Segment.GetAnimReference() || Segment.GetAnimReference()->IsValidAdditive()) return false;
	UE_LOG(LogTemp, Display, TEXT("GUNNER_ANIMATION_ASSETS_OK: montage sections, full-body slot and skeleton verified."));
	return true;
}
