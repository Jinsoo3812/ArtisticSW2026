#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayAbilitySpec.h"
#include "StatusReceiver.h"
#include "StatusComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class UStunGameplayAbility;

/** Reusable reception/lifecycle policy. No boss, weapon, or health-threshold rules. */
UCLASS(ClassGroup=(GAS), meta=(BlueprintSpawnableComponent))
class GASCORE_API UStatusComponent : public UActorComponent, public IStatusReceiver
{
	GENERATED_BODY()
public:
	UStatusComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION(BlueprintCallable, Category="GAS|Status")
	void InitializeWithAbilitySystem(UAbilitySystemComponent* InASC);
	void Uninitialize();
	virtual bool CanReceiveStatus_Implementation(FGameplayTag StatusTag) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="GAS|Status")
	FActiveGameplayEffectHandle ApplyStatus(TSubclassOf<UGameplayEffect> EffectClass,
		UAbilitySystemComponent* SourceASC, const FGameplayEffectContextHandle& Context, float Level = 1.f);

	UFUNCTION(BlueprintPure, Category="GAS|Status")
	bool HasStatus(FGameplayTag StatusTag) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="GAS|Status")
	void ClearStatuses();

	/** Only these exact identities are accepted. Ships have no component by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS|Status", meta=(Categories="State.Status"))
	FGameplayTagContainer SupportedStatuses;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Status", meta=(Categories="State.Status"))
	FGameplayTagContainer ImmuneStatuses;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS|Status")
	TSubclassOf<UStunGameplayAbility> StunAbilityClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS|Status")
	TObjectPtr<class UAnimMontage> StunMontage;
private:
	void HandleStunChanged(FGameplayTag Tag, int32 Count);
	void HandleDeathChanged(FGameplayTag Tag, int32 Count);
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> ASC;
	FDelegateHandle StunDelegate;
	FDelegateHandle DeathDelegate;
	FGameplayAbilitySpecHandle StunAbilityHandle;
};
