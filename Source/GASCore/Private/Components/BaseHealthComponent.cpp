// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BaseHealthComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseHealthFeedback, Log, All);

static TAutoConsoleVariable<int32> CVarSWDebugDeathImpactDirection(
	TEXT("sw.DeathRagdoll.DebugImpactDirection"),
	0,
	TEXT("Logs and draws the authoritative lethal-hit direction. 0=off, 1=on."),
	ECVF_Cheat);

UBaseHealthComponent::UBaseHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBaseHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBaseHealthComponent, DeathPresentation);
}

void UBaseHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeFromAbilitySystem();

	Super::EndPlay(EndPlayReason);
}

void UBaseHealthComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystemComponent)
{
	if (!InAbilitySystemComponent || AbilitySystemComponent == InAbilitySystemComponent)
	{
		return;
	}

	UninitializeFromAbilitySystem();

	AbilitySystemComponent = InAbilitySystemComponent;

	HealthChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UBaseHealthComponent::HandleHealthChanged);

	MaxHealthChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UBaseHealthComponent::HandleMaxHealthChanged);

	DamageChangedDelegateHandle = AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetDamageAttribute())
		.AddUObject(this, &UBaseHealthComponent::HandleDamageChanged);

	DeadTagDelegateHandle = AbilitySystemComponent
		->RegisterGameplayTagEvent(State_Dead)
		.AddUObject(this, &UBaseHealthComponent::HandleDeadTagChanged);

	if (AActor* Owner = GetOwningActor())
	{
		if (Owner->HasAuthority() && GetHealth() <= 0.0f)
		{
			StartDeath();
		}
	}
}

void UBaseHealthComponent::UninitializeFromAbilitySystem()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute())
		.Remove(HealthChangedDelegateHandle);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetMaxHealthAttribute())
		.Remove(MaxHealthChangedDelegateHandle);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetDamageAttribute())
		.Remove(DamageChangedDelegateHandle);

	AbilitySystemComponent
		->RegisterGameplayTagEvent(State_Dead)
		.Remove(DeadTagDelegateHandle);

	HealthChangedDelegateHandle.Reset();
	MaxHealthChangedDelegateHandle.Reset();
	DamageChangedDelegateHandle.Reset();
	DeadTagDelegateHandle.Reset();
	AbilitySystemComponent = nullptr;
	ClearPendingDamageContext();
}

float UBaseHealthComponent::GetHealth() const
{
	return AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute())
		: 0.0f;
}

float UBaseHealthComponent::GetMaxHealth() const
{
	return AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute())
		: 0.0f;
}

float UBaseHealthComponent::GetHealthNormalized() const
{
	const float MaxHealth = GetMaxHealth();
	return MaxHealth > 0.0f ? GetHealth() / MaxHealth : 0.0f;
}

void UBaseHealthComponent::StartDeath()
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || !AbilitySystemComponent
		|| DeathPresentation.DeathState != EBaseDeathState::NotDead)
	{
		return;
	}

	SetDeathState(EBaseDeathState::DeathStarted);

	if (!AbilitySystemComponent->HasMatchingGameplayTag(State_Dead))
	{
		AbilitySystemComponent->AddLooseGameplayTag(State_Dead, 1, EGameplayTagReplicationState::TagOnly);
	}

	FGameplayAbilitySpecHandle DeathAbilityHandle;
	for (const FGameplayAbilitySpec& AbilitySpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (AbilitySpec.Ability && AbilitySpec.Ability->GetClass()->IsChildOf(UBaseDeathGameplayAbility::StaticClass()))
		{
			DeathAbilityHandle = AbilitySpec.Handle;
			break;
		}
	}

	SendGameplayEventToOwner(GameplayAbility_Dead);

	if (DeathPresentation.DeathState != EBaseDeathState::DeathStarted)
	{
		return;
	}

	// Gameplay events activate matching abilities first. Explicit activation is
	// retained as a fallback for authored death abilities that lost their trigger.
	if (DeathAbilityHandle.IsValid())
	{
		FGameplayAbilitySpec* DeathSpec = AbilitySystemComponent->FindAbilitySpecFromHandle(DeathAbilityHandle);
		if (DeathSpec && !DeathSpec->IsActive())
		{
			AbilitySystemComponent->TryActivateAbility(DeathAbilityHandle);
			DeathSpec = AbilitySystemComponent->FindAbilitySpecFromHandle(DeathAbilityHandle);
		}

		if (DeathPresentation.DeathState != EBaseDeathState::DeathStarted || (DeathSpec && DeathSpec->IsActive()))
		{
			return;
		}

		UE_LOG(LogTemp, Error,
			TEXT("Death ability failed to activate; finishing death immediately. Owner=%s Ability=%s"),
			*GetNameSafe(Owner),
			DeathSpec && DeathSpec->Ability ? *GetNameSafe(DeathSpec->Ability) : TEXT("Invalid"));
	}

	// Characters without a death ability (all regular enemies) finish
	// immediately. Their owner has already applied immediate ragdoll from the
	// DeathStarted notification.
	FinishDeath();
}

FVector UBaseHealthComponent::CalculateKnockbackDirectionAwayFromSource(
	const FVector& VictimLocation,
	const FVector& SourceLocation)
{
	return (VictimLocation - SourceLocation).GetSafeNormal2D();
}

void UBaseHealthComponent::FinishDeath()
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || DeathPresentation.DeathState != EBaseDeathState::DeathStarted)
	{
		return;
	}

	SetDeathState(EBaseDeathState::DeathFinished);
}

bool UBaseHealthComponent::ResetForReuse()
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || !AbilitySystemComponent)
	{
		return false;
	}

	AbilitySystemComponent->CancelAllAbilities();
	AbilitySystemComponent->SetLooseGameplayTagCount(State_Dead, 0);
	ClearPendingDamageContext();
	DeathPresentation.ImpactData = FDeathRagdollImpactData();
	SetDeathState(EBaseDeathState::NotDead);
	AbilitySystemComponent->SetNumericAttributeBase(
		UBaseAttributeSet::GetHealthAttribute(),
		GetMaxHealth());
	return true;
}

void UBaseHealthComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	AActor* SourceActor = nullptr;
	FGameplayEffectContextHandle EffectContextHandle;
	FGameplayTag ImpactGameplayCueTag;
	if (Data.GEModData)
	{
		EffectContextHandle = Data.GEModData->EffectSpec.GetContext();
		SourceActor = ResolveSourceActorFromContext(EffectContextHandle);
		ImpactGameplayCueTag = ResolveImpactGameplayCueTag(Data.GEModData->EffectSpec);
	}

	if (!EffectContextHandle.IsValid() && bHasPendingDamageContext)
	{
		EffectContextHandle = PendingDamageEffectContextHandle;
	}

	if (!SourceActor && PendingDamageSourceActor.IsValid())
	{
		SourceActor = PendingDamageSourceActor.Get();
	}
	if (!Data.GEModData && !ImpactGameplayCueTag.IsValid())
	{
		ImpactGameplayCueTag = PendingImpactGameplayCueTag;
	}

	OnHealthChanged.Broadcast(this, Data.OldValue, Data.NewValue, SourceActor);

	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (Data.OldValue > Data.NewValue)
	{
		ExecuteConfirmedDamageGameplayCues(
			Data.OldValue - Data.NewValue,
			SourceActor,
			EffectContextHandle,
			ImpactGameplayCueTag);
	}

	if (Data.OldValue > Data.NewValue && Data.NewValue > 0.0f
		&& DeathPresentation.DeathState == EBaseDeathState::NotDead)
	{
		SendGameplayEventToOwner(GameplayAbility_HitReaction, Data.OldValue - Data.NewValue, SourceActor, EffectContextHandle);
		ClearPendingDamageContext();
	}

	if (Data.NewValue <= 0.0f)
	{
		if (DeathPresentation.DeathState == EBaseDeathState::NotDead)
		{
			CaptureLethalImpact(EffectContextHandle, SourceActor);
		}
		StartDeath();
		ClearPendingDamageContext();
	}
}

FGameplayTag UBaseHealthComponent::ResolveImpactGameplayCueTag(
	const FGameplayEffectSpec& EffectSpec) const
{
	FGameplayTag ResolvedTag;
	for (const FGameplayTag& Candidate : EffectSpec.GetDynamicAssetTags())
	{
		if (Candidate == GameplayCue_Impact || !Candidate.MatchesTag(GameplayCue_Impact))
		{
			continue;
		}
		if (ResolvedTag.IsValid() && ResolvedTag != Candidate)
		{
			UE_LOG(LogBaseHealthFeedback, Warning,
				TEXT("Damage spec contains multiple impact cues. Owner=%s First=%s Ignored=%s"),
				*GetNameSafe(GetOwningActor()), *ResolvedTag.ToString(), *Candidate.ToString());
			continue;
		}
		ResolvedTag = Candidate;
	}
	return ResolvedTag;
}

void UBaseHealthComponent::ExecuteConfirmedDamageGameplayCues(
	float DamageAmount,
	AActor* SourceActor,
	const FGameplayEffectContextHandle& EffectContextHandle,
	FGameplayTag ImpactGameplayCueTag) const
{
	if (!ShouldExecuteConfirmedDamageGameplayCues(DamageAmount, ImpactGameplayCueTag))
	{
		return;
	}
	AActor* Owner = GetOwningActor();

	FGameplayCueParameters Parameters(EffectContextHandle);
	Parameters.RawMagnitude = DamageAmount;
	Parameters.NormalizedMagnitude = GetMaxHealth() > 0.0f
		? FMath::Clamp(DamageAmount / GetMaxHealth(), 0.0f, 1.0f)
		: 0.0f;
	Parameters.EffectContext = EffectContextHandle;
	Parameters.Instigator = SourceActor;
	if (!Parameters.EffectCauser.IsValid())
	{
		Parameters.EffectCauser = SourceActor;
	}

	if (const FHitResult* HitResult = EffectContextHandle.GetHitResult())
	{
		Parameters.Location = HitResult->ImpactPoint;
		Parameters.Normal = HitResult->ImpactNormal;
		Parameters.PhysicalMaterial = HitResult->PhysMaterial.Get();
	}
	else
	{
		Parameters.Location = Owner->GetActorLocation();
		Parameters.Normal = SourceActor
			? (Owner->GetActorLocation() - SourceActor->GetActorLocation()).GetSafeNormal()
			: Owner->GetActorUpVector();
	}
	Parameters.TargetAttachComponent = Owner->GetRootComponent();
	Parameters.bReplicateLocationWhenUsingMinimalRepProxy = true;
	if (DamageGameplayCueTag.IsValid())
	{
		AbilitySystemComponent->ExecuteGameplayCue(DamageGameplayCueTag, Parameters);
	}
	if (ImpactGameplayCueTag.IsValid() && ImpactGameplayCueTag != DamageGameplayCueTag)
	{
		AbilitySystemComponent->ExecuteGameplayCue(ImpactGameplayCueTag, Parameters);
	}
}

bool UBaseHealthComponent::ShouldExecuteConfirmedDamageGameplayCues(
	float DamageAmount,
	FGameplayTag ImpactGameplayCueTag) const
{
	const AActor* Owner = GetOwningActor();
	return Owner && Owner->HasAuthority() && AbilitySystemComponent
		&& DamageAmount > 0.0f
		&& (DamageGameplayCueTag.IsValid() || ImpactGameplayCueTag.IsValid());
}

void UBaseHealthComponent::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	AActor* InstigatorActor = nullptr;
	if (Data.GEModData && Data.GEModData->EffectSpec.GetContext().GetOriginalInstigator())
	{
		InstigatorActor = Data.GEModData->EffectSpec.GetContext().GetOriginalInstigator();
	}

	OnMaxHealthChanged.Broadcast(this, Data.OldValue, Data.NewValue, InstigatorActor);
}

void UBaseHealthComponent::HandleDamageChanged(const FOnAttributeChangeData& Data)
{
	// UBaseAttributeSet resets Damage to zero before applying the resulting
	// Health change, so the falling edge must not clear the pending context.
	if (Data.NewValue <= Data.OldValue || Data.NewValue <= 0.0f || !Data.GEModData)
	{
		return;
	}

	PendingDamageEffectContextHandle = Data.GEModData->EffectSpec.GetContext();
	PendingDamageSourceActor = ResolveSourceActorFromContext(PendingDamageEffectContextHandle);
	PendingImpactGameplayCueTag = ResolveImpactGameplayCueTag(Data.GEModData->EffectSpec);
	bHasPendingDamageContext = PendingDamageEffectContextHandle.IsValid();
}

void UBaseHealthComponent::HandleDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (NewCount > 0 && DeathPresentation.DeathState == EBaseDeathState::NotDead)
	{
		SetDeathState(EBaseDeathState::DeathStarted);
	}
}

AActor* UBaseHealthComponent::ResolveSourceActorFromContext(const FGameplayEffectContextHandle& EffectContextHandle) const
{
	if (!EffectContextHandle.IsValid())
	{
		return nullptr;
	}

	if (AActor* SourceActor = EffectContextHandle.GetOriginalInstigator())
	{
		return SourceActor;
	}

	if (AActor* SourceActor = EffectContextHandle.GetEffectCauser())
	{
		return SourceActor;
	}

	return Cast<AActor>(EffectContextHandle.GetSourceObject());
}

void UBaseHealthComponent::ClearPendingDamageContext()
{
	PendingDamageEffectContextHandle = FGameplayEffectContextHandle();
	PendingImpactGameplayCueTag = FGameplayTag();
	PendingDamageSourceActor.Reset();
	bHasPendingDamageContext = false;
}

void UBaseHealthComponent::CaptureLethalImpact(
	const FGameplayEffectContextHandle& EffectContextHandle,
	AActor* SourceActor)
{
	DeathPresentation.ImpactData = BuildDeathRagdollImpactData(
		GetOwningActor(), EffectContextHandle, SourceActor);

	AActor* Owner = GetOwningActor();
	if (CVarSWDebugDeathImpactDirection.GetValueOnGameThread() > 0
		&& Owner && DeathPresentation.ImpactData.bHasDirection)
	{
		const FVector Direction =
			FVector(DeathPresentation.ImpactData.KnockbackDirection).GetSafeNormal2D();
		const FVector Start = DeathPresentation.ImpactData.bHasImpactPoint
			? FVector(DeathPresentation.ImpactData.ImpactPoint)
			: Owner->GetActorLocation();
		UE_LOG(LogBaseHealthFeedback, Display,
			TEXT("DeathImpact Owner=%s Source=%s Direction=%s Bone=%s"),
			*GetNameSafe(Owner),
			*GetNameSafe(SourceActor),
			*Direction.ToCompactString(),
			*DeathPresentation.ImpactData.HitBoneName.ToString());
		DrawDebugDirectionalArrow(
			Owner->GetWorld(), Start, Start + Direction * 250.0f,
			40.0f, FColor::Magenta, false, 5.0f, 0, 3.0f);
	}
}

FDeathRagdollImpactData UBaseHealthComponent::BuildDeathRagdollImpactData(
	const AActor* VictimActor,
	const FGameplayEffectContextHandle& EffectContextHandle,
	const AActor* SourceActor)
{
	FDeathRagdollImpactData Result;
	if (!VictimActor)
	{
		return Result;
	}

	const FHitResult* HitResult = EffectContextHandle.GetHitResult();
	if (HitResult)
	{
		Result.bHasImpactPoint = true;
		Result.ImpactPoint = HitResult->ImpactPoint;
		Result.HitBoneName = HitResult->BoneName;
	}

	FVector KnockbackDirection = FVector::ZeroVector;
	USWCombatEffectContextLibrary::GetImpactDirection(
		EffectContextHandle, KnockbackDirection);

	if (KnockbackDirection.IsNearlyZero())
	{
		// Legacy/environmental effects still use the same resolver, but new combat
		// paths should normally arrive with the direction already serialized.
		KnockbackDirection = USWCombatEffectContextLibrary::ResolveImpactDirection(
			SourceActor,
			EffectContextHandle.GetEffectCauser(),
			VictimActor,
			HitResult);
	}

	if (!KnockbackDirection.IsNearlyZero())
	{
		Result.bHasDirection = true;
		Result.KnockbackDirection = KnockbackDirection;
	}
	return Result;
}

void UBaseHealthComponent::SetDeathState(EBaseDeathState NewDeathState)
{
	if (DeathPresentation.DeathState == NewDeathState)
	{
		return;
	}

	const EBaseDeathState OldDeathState = DeathPresentation.DeathState;
	DeathPresentation.DeathState = NewDeathState;
	BroadcastDeathStateTransition(OldDeathState);
	if (AActor* Owner = GetOwningActor(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
}

void UBaseHealthComponent::BroadcastDeathStateTransition(EBaseDeathState OldDeathState)
{
	const EBaseDeathState NewDeathState = DeathPresentation.DeathState;
	// StartDeath and FinishDeath may coalesce into one replicated update. Preserve
	// the missing DeathStarted notification before broadcasting DeathFinished.
	if (OldDeathState == EBaseDeathState::NotDead
		&& NewDeathState == EBaseDeathState::DeathFinished)
	{
		OnDeathStarted.Broadcast(this);
	}

	if (NewDeathState == EBaseDeathState::DeathStarted)
	{
		OnDeathStarted.Broadcast(this);
	}
	else if (NewDeathState == EBaseDeathState::DeathFinished)
	{
		OnDeathFinished.Broadcast(this);
	}
}

void UBaseHealthComponent::SendGameplayEventToOwner(
	const FGameplayTag& EventTag,
	float EventMagnitude,
	AActor* SourceActor,
	const FGameplayEffectContextHandle& EffectContextHandle) const
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !EventTag.IsValid())
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = EventTag;
	Payload.Instigator = SourceActor;
	Payload.Target = Owner;
	Payload.OptionalObject = SourceActor;
	Payload.ContextHandle = EffectContextHandle;
	Payload.EventMagnitude = EventMagnitude;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, Payload);
}

AActor* UBaseHealthComponent::GetOwningActor() const
{
	return GetOwner();
}

void UBaseHealthComponent::OnRep_DeathPresentation(FReplicatedDeathPresentation OldPresentation)
{
	if (DeathPresentation.DeathState == OldPresentation.DeathState)
	{
		return;
	}
	BroadcastDeathStateTransition(OldPresentation.DeathState);
}
