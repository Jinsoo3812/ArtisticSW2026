// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "BaseHealthComponent.generated.h"

class UAbilitySystemComponent;
class UBaseHealthComponent;
struct FOnAttributeChangeData;
struct FGameplayEffectSpec;

UENUM(BlueprintType)
enum class EBaseDeathState : uint8
{
	NotDead,
	DeathStarted,
	DeathFinished
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FBaseHealthAttributeChangedSignature, UBaseHealthComponent*, HealthComponent, float, OldValue, float, NewValue, AActor*, InstigatorActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBaseHealthDeathEventSignature, UBaseHealthComponent*, HealthComponent);

UCLASS(ClassGroup=(GAS), meta=(BlueprintSpawnableComponent))
class GASCORE_API UBaseHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBaseHealthComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Call after ASC actor info is initialized. This binds Health, MaxHealth, and Dead tag changes.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystemComponent);

	// Unbinds ASC delegates. Call before the owner is destroyed or when replacing ASC.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void UninitializeFromAbilitySystem();

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthNormalized() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return DeathState != EBaseDeathState::NotDead; }

	UFUNCTION(BlueprintPure, Category = "Health")
	EBaseDeathState GetDeathState() const { return DeathState; }

	/** One-shot cue executed authoritatively for every confirmed health loss, including lethal damage. */
	UFUNCTION(BlueprintCallable, Category = "Health|Feedback")
	void SetDamageGameplayCueTag(FGameplayTag InGameplayCueTag) { DamageGameplayCueTag = InGameplayCueTag; }

	UFUNCTION(BlueprintPure, Category = "Health|Feedback")
	FGameplayTag GetDamageGameplayCueTag() const { return DamageGameplayCueTag; }

	// Starts death on the server. Adds State.Dead and sends the GameplayAbility.Dead event.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void StartDeath();

	// Call when the Death GA or death presentation has completed.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void FinishDeath();

	/** Authority-only reset used when a pooled actor is acquired again. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Health|Pooling")
	bool ResetForReuse();

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FBaseHealthAttributeChangedSignature OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FBaseHealthAttributeChangedSignature OnMaxHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FBaseHealthDeathEventSignature OnDeathStarted;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FBaseHealthDeathEventSignature OnDeathFinished;

private:
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);
	void HandleDamageChanged(const FOnAttributeChangeData& Data);
	void HandleDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void SetDeathState(EBaseDeathState NewDeathState);
	AActor* ResolveSourceActorFromContext(const FGameplayEffectContextHandle& EffectContextHandle) const;
	void ClearPendingDamageContext();
	FGameplayTag ResolveImpactGameplayCueTag(const FGameplayEffectSpec& EffectSpec) const;
	void ExecuteConfirmedDamageGameplayCues(
		float DamageAmount,
		AActor* SourceActor,
		const FGameplayEffectContextHandle& EffectContextHandle,
		FGameplayTag ImpactGameplayCueTag) const;
	void SendGameplayEventToOwner(
		const FGameplayTag& EventTag,
		float EventMagnitude = 0.0f,
		AActor* SourceActor = nullptr,
		const FGameplayEffectContextHandle& EffectContextHandle = FGameplayEffectContextHandle()) const;
	AActor* GetOwningActor() const;

	UFUNCTION()
	void OnRep_DeathState(EBaseDeathState OldDeathState);

private:
	/** Empty by default. Actor archetypes opt into their own replicated presentation cue. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag DamageGameplayCueTag;

	UPROPERTY(ReplicatedUsing = OnRep_DeathState)
	EBaseDeathState DeathState = EBaseDeathState::NotDead;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	FDelegateHandle HealthChangedDelegateHandle;
	FDelegateHandle MaxHealthChangedDelegateHandle;
	FDelegateHandle DamageChangedDelegateHandle;
	FDelegateHandle DeadTagDelegateHandle;

	FGameplayEffectContextHandle PendingDamageEffectContextHandle;
	FGameplayTag PendingImpactGameplayCueTag;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PendingDamageSourceActor;

	bool bHasPendingDamageContext = false;
};
