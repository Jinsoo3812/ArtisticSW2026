#include "Animation/EquipmentAnimationEditorLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AN_EquipmentAttachItem.h"

bool UEquipmentAnimationEditorLibrary::ConfigureEquipMontage(UAnimMontage* Montage, float AttachTime)
{
 if (!Montage || !Montage->GetSkeleton() || Montage->SlotAnimTracks.Num() != 1
  || AttachTime < 0.f || AttachTime >= Montage->GetPlayLength()) return false;
 USkeleton* Skeleton = Montage->GetSkeleton();
 if (!Skeleton->ContainsSlotName(TEXT("UpperBody"))) return false;
 Montage->Modify();
 Montage->SlotAnimTracks[0].SlotName = TEXT("UpperBody");
 Montage->BlendIn.SetBlendTime(0.15f);
 Montage->BlendOut.SetBlendTime(0.15f);
 Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Event) { return Cast<UAN_EquipmentAttachItem>(Event.Notify) != nullptr; });
 int32 TrackIndex = Montage->AnimNotifyTracks.IndexOfByPredicate([](const FAnimNotifyTrack& Track) { return Track.TrackName == TEXT("Equipment"); });
 if (TrackIndex == INDEX_NONE)
 {
  TrackIndex = Montage->AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("Equipment"), FLinearColor::Green));
 }
 FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
 Event.Notify = NewObject<UAN_EquipmentAttachItem>(Montage, NAME_None, RF_Transactional);
 Event.NotifyName = TEXT("Equipment Attach Item");
 Event.TrackIndex = TrackIndex;
 Event.Link(Montage, AttachTime);
 Event.MontageTickType = EMontageNotifyTickType::BranchingPoint;
 Event.TriggerWeightThreshold = 0.0001f;
 Event.bTriggerOnDedicatedServer = true;
 Event.bTriggerOnFollower = true;
 Montage->RefreshCacheData();
 Montage->PostEditChange();
 Montage->MarkPackageDirty();
 return true;
}
