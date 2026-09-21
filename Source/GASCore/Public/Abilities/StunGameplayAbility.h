#pragma once
#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "StunGameplayAbility.generated.h"

class AAIController;
class UCharacterMovementComponent;

/** GE owns Stun lifetime. This reaction owns cancellation and character/AI presentation. */
UCLASS(Blueprintable)
class GASCORE_API UStunGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()
public:
	UStunGameplayAbility();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
	void HandleStunChanged(FGameplayTag Tag, int32 Count);
	FDelegateHandle StunDelegate;
	TWeakObjectPtr<AAIController> LockedAI;
	TWeakObjectPtr<UCharacterMovementComponent> LockedMovement;
	bool bRestoreOrientToMovement = false;
	bool bRestoreDesiredRotation = false;
};
