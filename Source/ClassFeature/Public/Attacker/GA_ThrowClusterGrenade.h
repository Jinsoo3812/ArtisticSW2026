#pragma once

#include "Attacker/GA_ThrowGrenade.h"
#include "CoreMinimal.h"
#include "GA_ThrowClusterGrenade.generated.h"

UCLASS()
class CLASSFEATURE_API UGA_ThrowClusterGrenade : public UGA_ThrowGrenade {
  GENERATED_BODY()

protected:
  virtual void DrawTrajectory() override;

  // 기존 로직을 오버라이드하여, 투사체 스폰 시 PredictedSplitTime을 전달합니다.
  virtual void OnThrowEventReceived(FGameplayEventData Payload) override;

  // 산탄 분리까지 걸리는 예상 시간 (실제 투사체의 분리 시간과 궤적 길이에 모두 사용됨)
  UPROPERTY(EditDefaultsOnly, Category = "Throw")
  float PredictedSplitTime = 2.0f;

  virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
  virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

  /* Sub-munition Selection UI */
  UPROPERTY(EditDefaultsOnly, Category = "UI")
  TSubclassOf<class UClusterGrenadeSelectionWidget> SelectionWidgetClass;

  UPROPERTY()
  TObjectPtr<class UClusterGrenadeSelectionWidget> SelectionWidget;

  /* Sub-munition Mapping */
  UPROPERTY(EditDefaultsOnly, Category = "Cluster")
  TMap<FGameplayTag, TSubclassOf<class ASubMunitionProjectile>> SubMunitionClassMap;

  // Currently available sub-munition tags in inventory
  TArray<FGameplayTag> AvailableSubMunitions;
  int32 CurrentSelectionIndex = 0;

  UFUNCTION()
  void OnMouseWheelUp(FGameplayEventData Payload);

  UFUNCTION()
  void OnMouseWheelDown(FGameplayEventData Payload);

  void UpdateSelectionUI();

  // Tasks
  UPROPERTY()
  TObjectPtr<class UAbilityTask_WaitGameplayEvent> WheelUpTask;

  UPROPERTY()
  TObjectPtr<class UAbilityTask_WaitGameplayEvent> WheelDownTask;
};
