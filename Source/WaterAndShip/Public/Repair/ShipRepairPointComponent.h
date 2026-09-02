#pragma once

#include "CoreMinimal.h"
#include "InteractableComponent.h"
#include "GameplayTagContainer.h"
#include "ShipRepairPointComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class AShip;

UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class WATERANDSHIP_API UShipRepairPointComponent : public UInteractableComponent
{
	GENERATED_BODY()

public:
	UShipRepairPointComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Ship|Repair")
	bool IsLeakActive() const { return bLeakActive; }

	UFUNCTION(BlueprintPure, Category="Ship|Repair")
	bool IsBeingRepaired() const { return RepairingActor.IsValid(); }

	void ActivateLeak();
	void DeactivateLeak();
	void CancelRepair(AActor* RequestingActor = nullptr);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Repair")
	TObjectPtr<UNiagaraSystem> LeakNiagaraSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Repair", meta=(ClampMin="0.1", Units="s"))
	float RepairDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Repair", meta=(ClampMin="0.0", Units="cm"))
	float RepairCancelDistance = 220.0f;

protected:
	UFUNCTION()
	void HandleInteracted(AActor* Interactor);

	UFUNCTION()
	void OnRep_LeakActive();

	void UpdateVisualState();
	void ValidateRepair();
	void CompleteRepair();
	void ClearRepair(bool bCompleted);

	UPROPERTY(ReplicatedUsing=OnRep_LeakActive)
	bool bLeakActive = false;

	TWeakObjectPtr<AActor> RepairingActor;
	FGameplayTag PendingMaterialTag;
	FTimerHandle RepairCompletionTimer;
	FTimerHandle RepairValidationTimer;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveLeakComponent;
};
