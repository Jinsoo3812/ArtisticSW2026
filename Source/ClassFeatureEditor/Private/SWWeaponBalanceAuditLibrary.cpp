#include "SWWeaponBalanceAuditLibrary.h"

#include "Animation/AnimMontage.h"
#include "AnimNotify/AN_SendGameplayEvent.h"
#include "AnimNotify/ANS_HitScanWindow.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString USWWeaponBalanceAuditLibrary::InspectMontage(UAnimMontage* Montage)
{
	if (!Montage) return TEXT("{}");
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), Montage->GetPathName());
	Root->SetNumberField(TEXT("length"), Montage->GetPlayLength());
	Root->SetNumberField(TEXT("rate_scale"), Montage->RateScale);
	Root->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
	Root->SetNumberField(TEXT("blend_out_duration"), Montage->BlendOut.GetBlendTime());
	Root->SetBoolField(TEXT("auto_blend_out"), Montage->bEnableAutoBlendOut);
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (int32 Index = 0; Index < Montage->GetNumSections(); ++Index)
	{
		float Start, End;
		Montage->GetSectionStartAndEndTime(Index, Start, End);
		TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
		Section->SetStringField(TEXT("name"), Montage->GetSectionName(Index).ToString());
		Section->SetNumberField(TEXT("start"), Start);
		Section->SetNumberField(TEXT("end"), End);
		Sections.Add(MakeShared<FJsonValueObject>(Section));
	}
	Root->SetArrayField(TEXT("sections"), Sections);
	TArray<TSharedPtr<FJsonValue>> Events;
	auto AddEvent = [&Events](const FAnimNotifyEvent& Event, float Time, const FString& Source)
	{
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetNumberField(TEXT("time"), Time);
		Value->SetStringField(TEXT("source"), Source);
		Value->SetStringField(TEXT("class"), Event.Notify ? Event.Notify->GetClass()->GetPathName()
			: Event.NotifyStateClass ? Event.NotifyStateClass->GetClass()->GetPathName() : TEXT(""));
		FString Tag;
		if (const UAN_SendGameplayEvent* Notify = Cast<UAN_SendGameplayEvent>(Event.Notify)) Tag = Notify->GetEventTag().ToString();
		if (Cast<UANS_HitScanWindow>(Event.NotifyStateClass)) Tag = TEXT("Event.HandleScan.Start");
		Value->SetStringField(TEXT("event_tag"), Tag);
		Events.Add(MakeShared<FJsonValueObject>(Value));
	};
	for (const FAnimNotifyEvent& Event : Montage->Notifies) AddEvent(Event, Event.GetTime(), Montage->GetPathName());
	TArray<TSharedPtr<FJsonValue>> Segments;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
		{
			const UAnimSequenceBase* Sequence = Segment.GetAnimReference();
			if (!Sequence) continue;
			TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
			Value->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Value->SetNumberField(TEXT("start"), Segment.StartPos);
			Value->SetNumberField(TEXT("end"), Segment.GetEndPos());
			Value->SetNumberField(TEXT("play_rate"), Segment.GetValidPlayRate());
			Segments.Add(MakeShared<FJsonValueObject>(Value));
			for (const FAnimNotifyEvent& Event : Sequence->Notifies)
			{
				if (Segment.GetValidPlayRate() > 0.f && Event.GetTime() >= Segment.AnimStartTime && Event.GetTime() < Segment.AnimEndTime)
					AddEvent(Event, Segment.StartPos + (Event.GetTime() - Segment.AnimStartTime) / Segment.GetValidPlayRate(), Sequence->GetPathName());
			}
		}
	}
	Root->SetArrayField(TEXT("segments"), Segments);
	Root->SetArrayField(TEXT("notifies"), Events);
	TArray<TSharedPtr<FJsonValue>> Curve;
	for (int32 Sample = 0; Sample <= FMath::CeilToInt(Montage->GetPlayLength() / 0.005f); ++Sample)
	{
		const float Time = FMath::Min(Sample * 0.005f, Montage->GetPlayLength());
		float Alpha = 0.f;
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
				if (Segment.IsInRange(Time) && Segment.GetAnimReference())
					Alpha = FMath::Max(Alpha, Segment.GetAnimReference()->EvaluateCurveData(TEXT("Weapon.DrawAlpha"),
						FAnimExtractContext(static_cast<double>(Segment.ConvertTrackPosToAnimPos(Time)))));
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetNumberField(TEXT("time"), Time);
		Value->SetNumberField(TEXT("alpha"), Alpha);
		Curve.Add(MakeShared<FJsonValueObject>(Value));
	}
	Root->SetArrayField(TEXT("draw_curve"), Curve);
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
