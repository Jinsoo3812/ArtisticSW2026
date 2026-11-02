#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_MonitorStuckRecovery.generated.h"

class UStuckRecoveryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FStuckMonitoringReady);

/** Ends its unique monitoring source on every ability termination path. */
UCLASS()
class CLASSFEATURE_API UAbilityTask_MonitorStuckRecovery : public UAbilityTask
{
	GENERATED_BODY()
public:
	/** Continues the GA flow on both authority and predicted clients. */
	UPROPERTY(BlueprintAssignable)
	FStuckMonitoringReady OnReady;
	UFUNCTION(BlueprintCallable, Category="Ability|Tasks", meta=(DisplayName="Monitor Stuck Recovery",
		HidePin="OwningAbility", DefaultToSelf="OwningAbility", BlueprintInternalUseOnly="true"))
	static UAbilityTask_MonitorStuckRecovery* MonitorStuckRecovery(UGameplayAbility* OwningAbility);
	virtual void Activate() override;
protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;
private:
	TWeakObjectPtr<UStuckRecoveryComponent> Recovery;
	FGuid SourceId;
};
