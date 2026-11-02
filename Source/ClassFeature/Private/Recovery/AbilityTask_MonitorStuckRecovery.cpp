#include "Recovery/AbilityTask_MonitorStuckRecovery.h"

#include "Abilities/GameplayAbility.h"
#include "Recovery/StuckRecoveryComponent.h"

UAbilityTask_MonitorStuckRecovery* UAbilityTask_MonitorStuckRecovery::MonitorStuckRecovery(UGameplayAbility* OwningAbility)
{
	return NewAbilityTask<UAbilityTask_MonitorStuckRecovery>(OwningAbility);
}

void UAbilityTask_MonitorStuckRecovery::Activate()
{
	Super::Activate();
	AActor* Avatar = GetAvatarActor();
	if (!Avatar || !Avatar->HasAuthority())
	{
		if (ShouldBroadcastAbilityTaskDelegates()) OnReady.Broadcast();
		EndTask();
		return;
	}
	Recovery = Avatar->FindComponentByClass<UStuckRecoveryComponent>();
	if (!Recovery.IsValid())
	{
		if (ShouldBroadcastAbilityTaskDelegates()) OnReady.Broadcast();
		EndTask();
		return;
	}
	SourceId = Recovery->BeginMonitoring(Ability);
	if (ShouldBroadcastAbilityTaskDelegates()) OnReady.Broadcast();
}

void UAbilityTask_MonitorStuckRecovery::OnDestroy(bool bInOwnerFinished)
{
	if (Recovery.IsValid() && SourceId.IsValid()) Recovery->EndMonitoring(SourceId);
	SourceId.Invalidate();
	Super::OnDestroy(bInOwnerFinished);
}
