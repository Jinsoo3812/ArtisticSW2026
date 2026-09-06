#pragma once

#include "CoreMinimal.h"
#include "Skills/PlayerSkillGameplayAbility.h"
#include "GA_PlayerAreaSlow.generated.h"

class AAreaSlowTargetingDecal;
class ABasePlayer;
class UAreaSlowSkillDataAsset;

/**
 * Hold the mapped skill key for an owning-client-only range preview, confirm
 * with left click, or release/right-click to cancel before confirmation.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API UGA_PlayerAreaSlow : public UPlayerSkillGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_PlayerAreaSlow();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** Pure policy helper kept public for automation tests. */
	static bool IsEligibleTarget(
		const ABasePlayer* SourcePlayer,
		const AActor* Candidate,
		const UAreaSlowSkillDataAsset* InSkillData);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Area Slow")
	TObjectPtr<UAreaSlowSkillDataAsset> SkillData;

protected:
	UFUNCTION()
	void OnConfirmReceived();

	UFUNCTION()
	void OnCancelReceived();

	UFUNCTION()
	void OnActivationInputReleased(float TimeHeld);

private:
	void SpawnLocalTargetingPreview();
	void DestroyLocalTargetingPreview();
	int32 ExecuteAreaSlowOnServer();
	void SpawnConfirmedDecalOnServer(const struct FAreaSlowRange& Range);

	bool bExecutionRequested = false;
	bool bAimingStateTagApplied = false;
	bool bAbilityStateTagApplied = false;

	UPROPERTY(Transient)
	TObjectPtr<AAreaSlowTargetingDecal> LocalTargetingPreview;
};
