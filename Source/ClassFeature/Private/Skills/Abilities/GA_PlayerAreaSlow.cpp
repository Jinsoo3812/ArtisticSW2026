#include "Skills/Abilities/GA_PlayerAreaSlow.h"

#include "Abilities/Tasks/AbilityTask_WaitConfirmCancel.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Skills/AreaSlowDecalActors.h"
#include "Skills/AreaSlowSkillDataAsset.h"

UGA_PlayerAreaSlow::UGA_PlayerAreaSlow()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	SkillTag = GameplayAbility_Skill_AreaSlow;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_Skill_AreaSlow);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(State_Aiming);
}

void UGA_PlayerAreaSlow::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	FString FailureReason;
	if (!Player || !SkillData || !SkillData->IsRuntimeConfigValid(&FailureReason))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AreaSlow] Activation rejected. Player=%s Data=%s Reason=%s"),
			*GetNameSafe(Player), *GetNameSafe(SkillData), *FailureReason);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bExecutionRequested = false;
	bAimingStateTagApplied = false;
	bAbilityStateTagApplied = false;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Aiming);
		ASC->AddLooseGameplayTag(GameplayAbility_Skill_AreaSlow);
		bAimingStateTagApplied = true;
		bAbilityStateTagApplied = true;
	}
	Player->StopSprint();

	// Confirm/cancel travels through the ASC's generic replicated-event path.
	// Unlike an avatar RPC plus WaitGameplayEvent, this is keyed to this exact
	// predicted activation and cannot race the server-side ability instance.
	UAbilityTask_WaitConfirmCancel* ConfirmCancelTask =
		UAbilityTask_WaitConfirmCancel::WaitConfirmCancel(this);
	if (ConfirmCancelTask)
	{
		ConfirmCancelTask->OnConfirm.AddDynamic(this, &UGA_PlayerAreaSlow::OnConfirmReceived);
		ConfirmCancelTask->OnCancel.AddDynamic(this, &UGA_PlayerAreaSlow::OnCancelReceived);
		ConfirmCancelTask->ReadyForActivation();
	}

	UAbilityTask_WaitInputRelease* ReleaseTask =
		UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (ReleaseTask)
	{
		ReleaseTask->OnRelease.AddDynamic(this, &UGA_PlayerAreaSlow::OnActivationInputReleased);
		ReleaseTask->ReadyForActivation();
	}

	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		SpawnLocalTargetingPreview();
	}
}

void UGA_PlayerAreaSlow::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	DestroyLocalTargetingPreview();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (bAimingStateTagApplied)
		{
			ASC->RemoveLooseGameplayTag(State_Aiming);
		}
		if (bAbilityStateTagApplied)
		{
			ASC->RemoveLooseGameplayTag(GameplayAbility_Skill_AreaSlow);
		}
	}
	bAimingStateTagApplied = false;
	bAbilityStateTagApplied = false;
	bExecutionRequested = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_PlayerAreaSlow::OnConfirmReceived()
{
	if (!IsActive() || bExecutionRequested)
	{
		return;
	}

	bExecutionRequested = true;
	DestroyLocalTargetingPreview();
	if (bAimingStateTagApplied)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(State_Aiming);
		}
		bAimingStateTagApplied = false;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->HasAuthority())
	{
		// The local predicted instance waits for the authoritative instance to end it.
		return;
	}

	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo)
		|| !TryConsumeSkillUse())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	ExecuteAreaSlowOnServer();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_PlayerAreaSlow::OnCancelReceived()
{
	if (IsActive() && !bExecutionRequested)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UGA_PlayerAreaSlow::OnActivationInputReleased(float TimeHeld)
{
	if (IsActive() && !bExecutionRequested)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

bool UGA_PlayerAreaSlow::IsEligibleTarget(
	const ABasePlayer* SourcePlayer,
	const AActor* Candidate,
	const UAreaSlowSkillDataAsset* InSkillData)
{
	if (!SourcePlayer || !IsValid(Candidate) || Candidate == SourcePlayer || !InSkillData)
	{
		return false;
	}

	// Every player character is an invariant exclusion. Ships and other actors
	// are intentionally governed only by the data asset's explicit tag query.
	if (Candidate->IsA<ABasePlayer>())
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(Candidate));
	if (!TargetASC
		|| !TargetASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetMoveSpeedMultiplierAttribute())
		|| !TargetASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()))
	{
		return false;
	}

	FGameplayTagContainer OwnedTags;
	TargetASC->GetOwnedGameplayTags(OwnedTags);
	// Capability tags are an invariant implementation contract, independent of a
	// designer-customizable target query. This also keeps already-authored Data
	// Assets safe when the skill gains another affected attribute.
	if (!OwnedTags.HasTagExact(Capability_Effect_MoveSpeedMultiplier)
		|| !OwnedTags.HasTagExact(Capability_Effect_AttackSpeedMultiplier))
	{
		return false;
	}
	if (!InSkillData->RequiredTargetQuery.Matches(OwnedTags))
	{
		return false;
	}
	return InSkillData->BlockedTargetQuery.IsEmpty()
		|| !InSkillData->BlockedTargetQuery.Matches(OwnedTags);
}

void UGA_PlayerAreaSlow::SpawnLocalTargetingPreview()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->IsLocallyControlled() || !SkillData || !GetWorld())
	{
		return;
	}

	TSubclassOf<AAreaSlowTargetingDecal> PreviewClass = SkillData->TargetingDecalClass;
	if (!PreviewClass)
	{
		PreviewClass = AAreaSlowTargetingDecal::StaticClass();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Player;
	SpawnParameters.Instigator = Player;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	LocalTargetingPreview = GetWorld()->SpawnActor<AAreaSlowTargetingDecal>(
		PreviewClass,
		Player->GetActorLocation(),
		Player->GetActorRotation(),
		SpawnParameters);
	if (LocalTargetingPreview)
	{
		LocalTargetingPreview->SetReplicates(false);
		LocalTargetingPreview->ConfigurePreview(Player, SkillData);
	}
}

void UGA_PlayerAreaSlow::DestroyLocalTargetingPreview()
{
	if (IsValid(LocalTargetingPreview))
	{
		LocalTargetingPreview->Destroy();
	}
	LocalTargetingPreview = nullptr;
}

int32 UGA_PlayerAreaSlow::ExecuteAreaSlowOnServer()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!Player || !Player->HasAuthority() || !SourceASC || !SkillData || !GetWorld())
	{
		return 0;
	}

	const FAreaSlowRange Range = SkillData->BuildRangeForActor(Player);
	if (!Range.IsValid())
	{
		return 0;
	}

	FCollisionObjectQueryParams ObjectQuery;
	for (const EObjectTypeQuery ObjectType : SkillData->TargetObjectTypes)
	{
		const ECollisionChannel Channel = UEngineTypes::ConvertToCollisionChannel(ObjectType);
		if (Channel != ECC_MAX)
		{
			ObjectQuery.AddObjectTypesToQuery(Channel);
		}
	}
	if (!ObjectQuery.IsValid())
	{
		return 0;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerAreaSlow), false, Player);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		Range.Center,
		Range.Rotation.Quaternion(),
		ObjectQuery,
		FCollisionShape::MakeBox(Range.BoxExtent),
		QueryParams);

	if (SkillData->bDrawServerDebugBox)
	{
		DrawDebugBox(
			GetWorld(), Range.Center, Range.BoxExtent, Range.Rotation.Quaternion(),
			FColor::Cyan, false, 2.0f, 0, 3.0f);
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Player, Player);
	Context.AddSourceObject(SkillData);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		SkillData->SlowEffectClass,
		GetAbilityLevel(),
		Context);
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		SpawnConfirmedDecalOnServer(Range);
		return 0;
	}

	Spec.Data->SetDuration(SkillData->SlowDuration, true);
	Spec.Data->SetSetByCallerMagnitude(
		Data_Effect_MoveSpeedMultiplier,
		SkillData->MoveSpeedMultiplier);
	Spec.Data->SetSetByCallerMagnitude(
		Data_Effect_AttackSpeedMultiplier,
		SkillData->AttackSpeedMultiplier);
	Spec.Data->DynamicGrantedTags.AddTag(State_Debuff_Slow);

	TSet<AActor*> UniqueCandidates;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* Candidate = Overlap.GetActor())
		{
			UniqueCandidates.Add(Candidate);
		}
	}

	int32 AppliedTargetCount = 0;
	for (AActor* Candidate : UniqueCandidates)
	{
		if (!IsEligibleTarget(Player, Candidate, SkillData))
		{
			continue;
		}

		if (UAbilitySystemComponent* TargetASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate))
		{
			const FActiveGameplayEffectHandle AppliedHandle =
				TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			AppliedTargetCount += AppliedHandle.IsValid() ? 1 : 0;
		}
	}

	SpawnConfirmedDecalOnServer(Range);
	UE_LOG(LogTemp, Log,
		TEXT("[AreaSlow] Executed once. Candidates=%d Applied=%d Center=%s Extent=%s"),
		UniqueCandidates.Num(), AppliedTargetCount,
		*Range.Center.ToCompactString(), *Range.BoxExtent.ToCompactString());
	return AppliedTargetCount;
}

void UGA_PlayerAreaSlow::SpawnConfirmedDecalOnServer(const FAreaSlowRange& Range)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->HasAuthority() || !SkillData || !GetWorld())
	{
		return;
	}

	TSubclassOf<AAreaSlowConfirmedDecal> VisualClass = SkillData->ConfirmedDecalClass;
	if (!VisualClass)
	{
		VisualClass = AAreaSlowConfirmedDecal::StaticClass();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Player;
	SpawnParameters.Instigator = Player;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAreaSlowConfirmedDecal* ConfirmedVisual = GetWorld()->SpawnActor<AAreaSlowConfirmedDecal>(
		VisualClass,
		Range.Center,
		Range.Rotation,
		SpawnParameters);
	if (ConfirmedVisual)
	{
		ConfirmedVisual->InitializeConfirmedVisual(
			Range,
			SkillData->DecalProjectionDepth,
			SkillData->ConfirmedDecalMaterial,
			SkillData->ConfirmedDecalDuration);
	}
}
