#include "Attacker/GA_PlayerBasicAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Item/Weapons/SwordItem.h"

UGA_PlayerBasicAttack::UGA_PlayerBasicAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_BasicAttack);
	SetAssetTags(AssetTags);
}

void UGA_PlayerBasicAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bAttackFinished = false;
	bHitScanActive = false;
	bComboInputBuffered = false;
	bServerCombatPoseRefreshAcquired = false;
	CurrentComboIndex = INDEX_NONE;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->HasMatchingGameplayTag(State_Dead) || ASC->HasMatchingGameplayTag(State_Attacking))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CacheAttackData())
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_PlayerBasicAttack::ActivateAbility : Missing sword, attack montage, or damage data."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AddAttackStateTag();

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		Player->StopSprint();
		if (Player->HasAuthority())
		{
			Player->AcquireServerCombatPoseRefresh();
			bServerCombatPoseRefreshAcquired = true;
		}
	}

	HitScanStartEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_Start,
		nullptr,
		false,
		true);
	if (HitScanStartEventTask)
	{
		HitScanStartEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnHitScanStartEvent);
		HitScanStartEventTask->ReadyForActivation();
	}

	HitScanTickEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_Tick,
		nullptr,
		false,
		true);
	if (HitScanTickEventTask)
	{
		HitScanTickEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnHitScanTickEvent);
		HitScanTickEventTask->ReadyForActivation();
	}

	HitScanEndEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_HandleScan_End,
		nullptr,
		false,
		true);
	if (HitScanEndEventTask)
	{
		HitScanEndEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnHitScanEndEvent);
		HitScanEndEventTask->ReadyForActivation();
	}

	ComboCommitEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Event_Attack_ComboCommit,
		nullptr,
		false,
		true);
	if (ComboCommitEventTask)
	{
		ComboCommitEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnComboCommitEvent);
		ComboCommitEventTask->ReadyForActivation();
	}

	ComboInputEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		Key_Default_Mouse_LeftClick,
		nullptr,
		false,
		true);
	if (ComboInputEventTask)
	{
		ComboInputEventTask->EventReceived.AddDynamic(this, &UGA_PlayerBasicAttack::OnComboInputEvent);
		ComboInputEventTask->ReadyForActivation();
	}

	if (!PlayAttackMontage())
	{
		FinishAttack(true);
	}
}

void UGA_PlayerBasicAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	EndHitScan();

	if (bServerCombatPoseRefreshAcquired)
	{
		if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
		{
			Player->ReleaseServerCombatPoseRefresh();
		}
		bServerCombatPoseRefreshAcquired = false;
	}

	RemoveAttackStateTag();

	CachedSword = nullptr;
	CachedAttackMontage = nullptr;
	CachedComboSections.Reset();
	CachedAttackMontagePlayRate = 1.0f;
	CachedDamageSpecHandle = FGameplayEffectSpecHandle();
	AttackMontageTask = nullptr;
	HitScanStartEventTask = nullptr;
	HitScanTickEventTask = nullptr;
	HitScanEndEventTask = nullptr;
	ComboCommitEventTask = nullptr;
	ComboInputEventTask = nullptr;
	CurrentComboIndex = INDEX_NONE;
	bComboInputBuffered = false;
	bAttackFinished = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGA_PlayerBasicAttack::CacheAttackData()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || Player->IsEquipmentTransitioning())
	{
		return false;
	}

	CachedSword = Cast<ASwordItem>(Player->EquippedItem);
	UPlayerEquipmentComponent* EquipmentComponent = Player->GetEquipmentComponent();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!CachedSword || !EquipmentComponent || !SourceASC)
	{
		return false;
	}

	CachedAttackMontage = EquipmentComponent->GetEquippedBasicAttackMontage();
	CachedAttackMontagePlayRate = EquipmentComponent->GetEquippedBasicAttackPlayRate();
	if (!CachedAttackMontage || !CacheComboSections(EquipmentComponent->GetEquippedBasicAttackComboSections()))
	{
		return false;
	}

	TSubclassOf<UGameplayEffect> DamageEffectClass = CachedSword->GetDamageEffectClass();
	if (!DamageEffectClass)
	{
		DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	}

	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = SourceASC;
	DamageRequest.DamageEffectClass = DamageEffectClass;
	DamageRequest.AttackCoefficient = CachedSword->GetAttackCoefficient();
	DamageRequest.ChargeMultiplier = 1.0f;
	DamageRequest.InstigatorActor = Player;
	DamageRequest.EffectCauser = CachedSword;
	DamageRequest.EffectLevel = GetAbilityLevel();
	CachedDamageSpecHandle = UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest);

	return CachedDamageSpecHandle.IsValid();
}

bool UGA_PlayerBasicAttack::CacheComboSections(const TArray<FName>& ConfiguredSections)
{
	CachedComboSections.Reset();
	if (!CachedAttackMontage)
	{
		return false;
	}

	if (ConfiguredSections.IsEmpty())
	{
		for (int32 SectionIndex = 0; SectionIndex < CachedAttackMontage->GetNumSections(); ++SectionIndex)
		{
			const FName SectionName = CachedAttackMontage->GetSectionName(SectionIndex);
			if (!SectionName.IsNone())
			{
				CachedComboSections.Add(SectionName);
			}
		}

		return true;
	}

	for (const FName SectionName : ConfiguredSections)
	{
		if (SectionName.IsNone() || CachedAttackMontage->GetSectionIndex(SectionName) == INDEX_NONE)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("UGA_PlayerBasicAttack::CacheComboSections : Montage '%s' does not contain section '%s'."),
				*GetNameSafe(CachedAttackMontage),
				*SectionName.ToString());
			return false;
		}

		if (CachedComboSections.Contains(SectionName))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("UGA_PlayerBasicAttack::CacheComboSections : Duplicate combo section '%s'."),
				*SectionName.ToString());
			return false;
		}

		CachedComboSections.Add(SectionName);
	}

	return true;
}

bool UGA_PlayerBasicAttack::PlayAttackMontage()
{
	if (!CachedAttackMontage)
	{
		return false;
	}

	AttackMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("PlayerBasicAttackMontageTask")),
		CachedAttackMontage,
		FMath::Max(CachedAttackMontagePlayRate, KINDA_SMALL_NUMBER),
		CachedComboSections.IsEmpty() ? NAME_None : CachedComboSections[0],
		true);

	if (!AttackMontageTask)
	{
		return false;
	}

	AttackMontageTask->OnCompleted.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageCompleted);
	AttackMontageTask->OnBlendOut.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageBlendOut);
	AttackMontageTask->OnInterrupted.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageInterrupted);
	AttackMontageTask->OnCancelled.AddDynamic(this, &UGA_PlayerBasicAttack::OnAttackMontageCancelled);
	AttackMontageTask->ReadyForActivation();

	if (!CachedComboSections.IsEmpty())
	{
		CurrentComboIndex = 0;
		if (CachedComboSections.IsValidIndex(CurrentComboIndex + 1))
		{
			HoldSectionForCommit(CachedComboSections[CurrentComboIndex]);
		}
	}

	return true;
}

void UGA_PlayerBasicAttack::CommitBufferedCombo()
{
	if (bAttackFinished || !CachedComboSections.IsValidIndex(CurrentComboIndex))
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		FinishAttack(true);
		return;
	}

	const FName CurrentSection = CachedComboSections[CurrentComboIndex];
	const int32 NextComboIndex = CurrentComboIndex + 1;
	if (!bComboInputBuffered || !CachedComboSections.IsValidIndex(NextComboIndex))
	{
		ASC->CurrentMontageSetNextSectionName(CurrentSection, NAME_None);
		bComboInputBuffered = false;
		return;
	}

	const FName NextSection = CachedComboSections[NextComboIndex];

	// Only sections that have another combo step need to wait for a commit.
	// The final section remains terminal and finishes without another notify.
	if (CachedComboSections.IsValidIndex(NextComboIndex + 1))
	{
		HoldSectionForCommit(NextSection);
	}
	else
	{
		ASC->CurrentMontageSetNextSectionName(NextSection, NAME_None);
	}
	ASC->CurrentMontageSetNextSectionName(CurrentSection, NextSection);

	CurrentComboIndex = NextComboIndex;
	bComboInputBuffered = false;
}

void UGA_PlayerBasicAttack::HoldSectionForCommit(FName SectionName)
{
	if (SectionName.IsNone())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageSetNextSectionName(SectionName, SectionName);
	}
}

void UGA_PlayerBasicAttack::OnAttackMontageCompleted()
{
	FinishAttack(false);
}

void UGA_PlayerBasicAttack::OnAttackMontageBlendOut()
{
	// A queued Anim Notify can be dispatched later in the same frame than this
	// callback. Keep the ability and its WaitGameplayEvent tasks alive until the
	// montage completes so a late combo commit event is still observable.
}

void UGA_PlayerBasicAttack::OnAttackMontageInterrupted()
{
	FinishAttack(true);
}

void UGA_PlayerBasicAttack::OnAttackMontageCancelled()
{
	FinishAttack(true);
}

void UGA_PlayerBasicAttack::OnHitScanStartEvent(FGameplayEventData Payload)
{
	StartHitScan();
}

void UGA_PlayerBasicAttack::OnHitScanTickEvent(FGameplayEventData Payload)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority() && bHitScanActive && CachedSword)
	{
		CachedSword->SampleHitScan();
	}
}

void UGA_PlayerBasicAttack::OnHitScanEndEvent(FGameplayEventData Payload)
{
	EndHitScan();
}

void UGA_PlayerBasicAttack::OnComboCommitEvent(FGameplayEventData Payload)
{
	CommitBufferedCombo();
}

void UGA_PlayerBasicAttack::OnComboInputEvent(FGameplayEventData Payload)
{
	// The input router marks the activation click as 0 and presses made while
	// this input's ability was already active as 1.
	const bool bIsRepeatInput = Payload.EventMagnitude > 0.5f;
	if (bIsRepeatInput && !bAttackFinished && CachedComboSections.IsValidIndex(CurrentComboIndex + 1))
	{
		bComboInputBuffered = true;
	}
}

void UGA_PlayerBasicAttack::StartHitScan()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || bHitScanActive || !CachedSword || !CachedDamageSpecHandle.IsValid())
	{
		return;
	}

	bHitScanActive = CachedSword->HitScanStart(CachedDamageSpecHandle);
}

void UGA_PlayerBasicAttack::EndHitScan()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority() && CachedSword)
	{
		CachedSword->HitScanEnd();
	}

	bHitScanActive = false;
}

void UGA_PlayerBasicAttack::FinishAttack(bool bWasCancelled)
{
	if (bAttackFinished)
	{
		return;
	}

	bAttackFinished = true;
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

void UGA_PlayerBasicAttack::AddAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}
}

void UGA_PlayerBasicAttack::RemoveAttackStateTag()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}
