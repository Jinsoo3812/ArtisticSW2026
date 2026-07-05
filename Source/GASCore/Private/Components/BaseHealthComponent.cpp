// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BaseHealthComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

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
		->RegisterGameplayTagEvent(State_Dead)
		.Remove(DeadTagDelegateHandle);

	HealthChangedDelegateHandle.Reset();
	MaxHealthChangedDelegateHandle.Reset();
	DeadTagDelegateHandle.Reset();
	AbilitySystemComponent = nullptr;
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

void UBaseHealthComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	AActor* InstigatorActor = nullptr;
	if (Data.GEModData && Data.GEModData->EffectSpec.GetContext().GetOriginalInstigator())
	{
		InstigatorActor = Data.GEModData->EffectSpec.GetContext().GetOriginalInstigator();
	}

	OnHealthChanged.Broadcast(this, Data.OldValue, Data.NewValue, InstigatorActor);

	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (Data.OldValue > Data.NewValue && Data.NewValue > 0.0f && DeathState == EBaseDeathState::NotDead)
	{
		SendGameplayEventToOwner(GameplayAbility_HitReaction, Data.OldValue - Data.NewValue);
	}

	if (Data.NewValue <= 0.0f)
	{
		StartDeath();
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

void UBaseHealthComponent::HandleDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (NewCount > 0 && DeathState == EBaseDeathState::NotDead)
	{
		SetDeathState(EBaseDeathState::DeathStarted);
	}
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

void UBaseHealthComponent::SendGameplayEventToOwner(const FGameplayTag& EventTag, float EventMagnitude) const
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !EventTag.IsValid())
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = EventTag;
	Payload.Instigator = Owner;
	Payload.Target = Owner;
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
