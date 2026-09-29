#include "SWRoomAbilitySystemComponent.h"

#include "GameplayEffect.h"
#include "GASStrengthEquipmentGameplayEffect.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void USWRoomAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RemovedEffectDelegateHandle.IsValid())
		OnAnyGameplayEffectRemovedDelegate().Remove(RemovedEffectDelegateHandle);
	RemovedEffectDelegateHandle.Reset();
	RestoredSourceMetadata.Reset();
	Super::EndPlay(EndPlayReason);
}

void USWRoomAbilitySystemComponent::HandleRoomEffectRemoved(const FActiveGameplayEffect& Effect)
{
	RestoredSourceMetadata.Remove(Effect.Handle);
}

void USWRoomAbilitySystemComponent::CaptureRoomEffects(
	TArray<FSWRoomGameplayEffectState>& OutEffects, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	OutEffects.Reset();
	TArray<FActiveGameplayEffectHandle> Handles = GetActiveEffects(FGameplayEffectQuery());
	Handles.Sort([this](const FActiveGameplayEffectHandle& Left, const FActiveGameplayEffectHandle& Right)
	{
		const FActiveGameplayEffect* A = GetActiveGameplayEffect(Left);
		const FActiveGameplayEffect* B = GetActiveGameplayEffect(Right);
		const FString AClass = A && A->Spec.Def ? A->Spec.Def->GetClass()->GetPathName() : FString();
		const FString BClass = B && B->Spec.Def ? B->Spec.Def->GetClass()->GetPathName() : FString();
		return AClass == BClass
			? (A ? A->GetTimeRemaining(GetWorld()->GetTimeSeconds()) : 0.f)
				< (B ? B->GetTimeRemaining(GetWorld()->GetTimeSeconds()) : 0.f)
			: AClass < BClass;
	});
	TMap<FString, int32> ClassOrdinals;
	for (const FActiveGameplayEffectHandle Handle : Handles)
	{
		const FActiveGameplayEffect* Active = GetActiveGameplayEffect(Handle);
		if (!Active || !Active->Spec.Def || Active->Spec.Def->DurationPolicy == EGameplayEffectDurationType::Instant
			|| Active->Spec.Def->IsA<UGASStrengthEquipmentGameplayEffect>()) continue;
		const FString ClassPath = Active->Spec.Def->GetClass()->GetPathName();
		const int32 Ordinal = ClassOrdinals.FindOrAdd(ClassPath)++;
		FSWRoomGameplayEffectState& Saved = OutEffects.AddDefaulted_GetRef();
		Saved.EffectClass = FSoftClassPath(Active->Spec.Def->GetClass());
		Saved.Level = Active->Spec.GetLevel();
		Saved.StackCount = Active->Spec.GetStackCount();
		if (const FGameplayTagContainer* Tags = Active->Spec.CapturedSourceTags.GetAggregatedTags())
			Saved.CapturedSourceTags = *Tags;
		Saved.DurationRemaining = Active->GetTimeRemaining(GetWorld()->GetTimeSeconds());
		if (Active->GetPeriod() > 0.f)
			Saved.NextPeriodRemaining = GetWorld()->GetTimerManager().GetTimerRemaining(Active->PeriodHandle);
		for (const TPair<FGameplayTag, float>& Pair : Active->Spec.SetByCallerTagMagnitudes)
		{
			FSWRoomSetByCallerTagValue& Value = Saved.TagMagnitudes.AddDefaulted_GetRef();
			Value.Tag = Pair.Key;
			Value.Value = Pair.Value;
		}
		for (const TPair<FName, float>& Pair : Active->Spec.SetByCallerNameMagnitudes)
		{
			FSWRoomSetByCallerNameValue& Value = Saved.NameMagnitudes.AddDefaulted_GetRef();
			Value.Name = Pair.Key;
			Value.Value = Pair.Value;
		}
		Saved.TagMagnitudes.Sort([](const FSWRoomSetByCallerTagValue& A, const FSWRoomSetByCallerTagValue& B)
		{ return A.Tag.ToString() < B.Tag.ToString(); });
		Saved.NameMagnitudes.Sort([](const FSWRoomSetByCallerNameValue& A, const FSWRoomSetByCallerNameValue& B)
		{ return A.Name.LexicalLess(B.Name); });
		if (const AActor* Source = Active->Spec.GetContext().GetInstigator())
		{
			Saved.SourceClass = FSoftClassPath(Source->GetClass());
			if (const USWRoomSnapshotComponent* Id = Source->FindComponentByClass<USWRoomSnapshotComponent>())
				Saved.SourceStableId = Id->StableId;
		}
		else if (const FSWRoomGameplayEffectState* Metadata = RestoredSourceMetadata.Find(Handle))
		{
			Saved.SourceStableId = Metadata->SourceStableId;
			Saved.SourceClass = Metadata->SourceClass;
			Saved.CapturedSourceTags = Metadata->CapturedSourceTags;
			Saved.bSourceMissingAllowed = Metadata->bSourceMissingAllowed;
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (const USWRoomSnapshotComponent* Id = It->FindComponentByClass<USWRoomSnapshotComponent>();
					Id && Id->StableId == Metadata->SourceStableId)
				{
					Saved.SourceClass = FSoftClassPath(It->GetClass());
					break;
				}
		}
		if (Saved.EffectClass.IsNull() || !FMath::IsFinite(Saved.Level)
			|| (Saved.DurationRemaining < 0.f && Active->Spec.Def->DurationPolicy != EGameplayEffectDurationType::Infinite)
			|| (Active->GetPeriod() > 0.f && Saved.NextPeriodRemaining < 0.f))
		{
			OutEffects.Pop();
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("GameplayEffect");
			Issue.FieldKey = FName(*(ClassPath + TEXT("#") + FString::FromInt(Ordinal)));
			Issue.Reason = TEXT("Active effect class, duration, or periodic phase unavailable");
		}
		else if (Saved.SourceStableId.IsValid() && !Active->Spec.GetContext().GetInstigator()
			&& !Active->Spec.Def->Executions.IsEmpty())
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("GameplayEffect");
			Issue.FieldKey = FName(*(ClassPath + TEXT("/SourceAttributes")));
			Issue.Reason = TEXT("Restored source is absent; execution source attributes cannot be recaptured");
		}
	}
	OutEffects.Sort([](const FSWRoomGameplayEffectState& A, const FSWRoomGameplayEffectState& B)
	{ return A.EffectClass == B.EffectClass ? A.DurationRemaining < B.DurationRemaining
		: A.EffectClass.ToString() < B.EffectClass.ToString(); });
}

bool USWRoomAbilitySystemComponent::RestoreRoomEffects(
	const TArray<FSWRoomGameplayEffectState>& Effects, FString& OutError)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bRestoringRoomEffects)
	{
		OutError = TEXT("Effect restore unavailable");
		return false;
	}
	TGuardValue<bool> RestoreGuard(bRestoringRoomEffects, true);
	if (!RemovedEffectDelegateHandle.IsValid())
		RemovedEffectDelegateHandle = OnAnyGameplayEffectRemovedDelegate().AddUObject(
			this, &USWRoomAbilitySystemComponent::HandleRoomEffectRemoved);
	for (const FActiveGameplayEffectHandle Handle : GetActiveEffects(FGameplayEffectQuery()))
	{
		const FActiveGameplayEffect* Active = GetActiveGameplayEffect(Handle);
		if (Active && Active->Spec.Def && Active->Spec.Def->IsA<UGASStrengthEquipmentGameplayEffect>()) continue;
		RemoveActiveGameplayEffect(Handle);
	}
	for (const FSWRoomGameplayEffectState& Saved : Effects)
	{
		UClass* EffectClass = Saved.EffectClass.TryLoadClass<UGameplayEffect>();
		UGameplayEffect* Definition = EffectClass ? EffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
		if (!Definition || Definition->DurationPolicy == EGameplayEffectDurationType::Instant
			|| !FMath::IsFinite(Saved.Level) || Saved.Level <= 0.f || Saved.StackCount < 1
			|| !FMath::IsFinite(Saved.DurationRemaining) || !FMath::IsFinite(Saved.NextPeriodRemaining))
		{
			OutError = FString::Printf(TEXT("Invalid saved gameplay effect: %s"), *Saved.EffectClass.ToString());
			return false;
		}
		FGameplayEffectContextHandle Context = MakeEffectContext();
		AActor* SavedSource = nullptr;
		if (Saved.SourceStableId.IsValid())
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (const USWRoomSnapshotComponent* Id = It->FindComponentByClass<USWRoomSnapshotComponent>();
					Id && Id->StableId == Saved.SourceStableId)
				{
					SavedSource = *It;
					break;
				}
		if (SavedSource && (Saved.SourceClass.IsNull() || FSoftClassPath(SavedSource->GetClass()) == Saved.SourceClass))
			Context.AddInstigator(SavedSource, SavedSource);
		else if (Saved.SourceStableId.IsValid() && !Saved.bSourceMissingAllowed)
		{
			OutError = FString::Printf(TEXT("Required gameplay effect source missing: Effect=%s Source=%s"),
				*Saved.EffectClass.ToString(), *Saved.SourceStableId.ToString());
			return false;
		}
		FGameplayEffectSpec Spec(Definition, Context, Saved.Level);
		Spec.CapturedSourceTags.GetSpecTags().AppendTags(Saved.CapturedSourceTags);
		if (Definition->DurationPolicy == EGameplayEffectDurationType::HasDuration)
			Spec.SetDuration(FMath::Max(Saved.DurationRemaining, KINDA_SMALL_NUMBER), true);
		Spec.SetStackCount(Saved.StackCount);
		for (const FSWRoomSetByCallerTagValue& Value : Saved.TagMagnitudes)
			Spec.SetSetByCallerMagnitude(Value.Tag, Value.Value);
		for (const FSWRoomSetByCallerNameValue& Value : Saved.NameMagnitudes)
			Spec.SetSetByCallerMagnitude(Value.Name, Value.Value);
		TGuardValue<bool> PeriodicApplicationGuard(Definition->bExecutePeriodicEffectOnApplication, false);
		const FActiveGameplayEffectHandle Handle = ApplyGameplayEffectSpecToSelf(Spec);
		if (!Handle.IsValid())
		{
			OutError = FString::Printf(TEXT("Gameplay effect application failed: %s"), *Saved.EffectClass.ToString());
			return false;
		}
		if (Saved.SourceStableId.IsValid() && !SavedSource)
			RestoredSourceMetadata.Add(Handle, Saved);
		if (FActiveGameplayEffect* Active = ActiveGameplayEffects.GetActiveGameplayEffect(Handle))
			if (Active->GetPeriod() > 0.f)
			{
				GetWorld()->GetTimerManager().ClearTimer(Active->PeriodHandle);
				FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this,
					&USWRoomAbilitySystemComponent::ExecutePeriodicEffect, Handle);
				GetWorld()->GetTimerManager().SetTimer(Active->PeriodHandle, Delegate, Active->GetPeriod(), true,
					FMath::Max(Saved.NextPeriodRemaining, KINDA_SMALL_NUMBER));
			}
	}
	return true;
}
