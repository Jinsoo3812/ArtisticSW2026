// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BaseHealthComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseHealthFeedback, Log, All);

UBaseHealthComponent::UBaseHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBaseHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBaseHealthComponent, DeathState);
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
	if (!Owner || !Owner->HasAuthority() || !AbilitySystemComponent || DeathState != EBaseDeathState::NotDead)
	{
		return;
	}

	SetDeathState(EBaseDeathState::DeathStarted);

	if (!AbilitySystemComponent->HasMatchingGameplayTag(State_Dead))
	{
		AbilitySystemComponent->AddLooseGameplayTag(State_Dead, 1, EGameplayTagReplicationState::TagOnly);
	}

	SendGameplayEventToOwner(GameplayAbility_Dead);

	for (const FGameplayAbilitySpec& AbilitySpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (AbilitySpec.Ability && AbilitySpec.Ability->GetClass()->IsChildOf(UBaseDeathGameplayAbility::StaticClass()))
		{
			AbilitySystemComponent->TryActivateAbility(AbilitySpec.Handle);
			break;
		}
	}
}

void UBaseHealthComponent::FinishDeath()
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || DeathState != EBaseDeathState::DeathStarted)
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

	if (Data.OldValue > Data.NewValue && Data.NewValue > 0.0f && DeathState == EBaseDeathState::NotDead)
	{
		SendGameplayEventToOwner(GameplayAbility_HitReaction, Data.OldValue - Data.NewValue, SourceActor, EffectContextHandle);
		ClearPendingDamageContext();
	}

	if (Data.NewValue <= 0.0f)
	{
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
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || !AbilitySystemComponent
		|| DamageAmount <= 0.0f
		|| (!DamageGameplayCueTag.IsValid() && !ImpactGameplayCueTag.IsValid()))
	{
		return;
	}

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
	if (NewCount > 0 && DeathState == EBaseDeathState::NotDead)
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

void UBaseHealthComponent::SetDeathState(EBaseDeathState NewDeathState)
{
	if (DeathState == NewDeathState)
	{
		return;
	}

	const EBaseDeathState OldDeathState = DeathState;
	DeathState = NewDeathState;

	if (NewDeathState == EBaseDeathState::DeathStarted)
	{
		OnDeathStarted.Broadcast(this);
	}
	else if (NewDeathState == EBaseDeathState::DeathFinished)
	{
		OnDeathFinished.Broadcast(this);
	}

	(void)OldDeathState;
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

void UBaseHealthComponent::OnRep_DeathState(EBaseDeathState OldDeathState)
{
	if (DeathState == OldDeathState)
	{
		return;
	}

	if (DeathState == EBaseDeathState::DeathStarted)
	{
		OnDeathStarted.Broadcast(this);
	}
	else if (DeathState == EBaseDeathState::DeathFinished)
	{
		OnDeathFinished.Broadcast(this);
	}
}
