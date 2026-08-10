#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyShipNavigationComponent.generated.h"

class AEnemyShip;
class AShip;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnEnemyShipNavigationStateChanged,
	ENavalCombatState, PreviousState,
	ENavalCombatState, NewState);

/**
 * Owns all persistent naval steering state and is the only Enemy-side writer
 * of AShip AI control input. BT and Gameplay Abilities submit intent only.
 */
UCLASS(ClassGroup = (EnemyShip), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyShipNavigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyShipNavigationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetNavigationEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	bool IsNavigationEnabled() const { return bNavigationEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetNavigationProfile(const FEnemyShipNavigationProfile& InProfile);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	const FEnemyShipNavigationProfile& GetNavigationProfile() const { return NavigationProfile; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetTargetShip(AShip* InTargetShip);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	AShip* GetTargetShip() const { return TargetShip.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Enemy Ship|Navigation")
	void SetHomeActor(AActor* InHomeActor);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	AActor* GetHomeActor() const { return HomeActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	ENavalCombatState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Navigation")
	FEnemyShipNavigationOutput GetLastNavigationOutput() const { return LastNavigationOutput; }

	FEnemyShipNavigationOverrideHandle AcquireOverride(
		UObject* Requester,
		int32 Priority,
		const FEnemyShipNavigationOverrideRequest& Request);

	bool UpdateOverride(
		FEnemyShipNavigationOverrideHandle Handle,
		const FEnemyShipNavigationOverrideRequest& Request);

	bool ReleaseOverride(FEnemyShipNavigationOverrideHandle Handle);
	void ReleaseOverridesFor(UObject* Requester);
	void ClearAllOverrides();
	bool HasActiveOverride() const;

	UPROPERTY(BlueprintAssignable, Category = "Enemy Ship|Navigation")
	FOnEnemyShipNavigationStateChanged OnNavigationStateChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Ship|Navigation")
	FEnemyShipNavigationProfile NavigationProfile;

private:
	struct FRuntimeOverride
	{
		TWeakObjectPtr<UObject> Requester;
		int32 Priority = 0;
		uint64 Sequence = 0;
		FEnemyShipNavigationOverrideRequest Request;
	};

	FEnemyShipNavigationContext BuildContext() const;
	void RemoveInvalidOverrides();
	const FRuntimeOverride* FindWinningOverride() const;
	void ApplySquadAvoidance(FEnemyShipNavigationOutput& InOutOutput);
	void ApplyControl(const FEnemyShipNavigationOutput& BaseOutput);
	void StopOwnerShip();

	TWeakObjectPtr<AEnemyShip> OwnerShip;
	TWeakObjectPtr<AShip> TargetShip;
	TWeakObjectPtr<AActor> HomeActor;
	TMap<FGuid, FRuntimeOverride> Overrides;
	uint64 NextOverrideSequence = 1;
	ENavalCombatState CurrentState = ENavalCombatState::Idle;
	FEnemyShipNavigationOutput LastNavigationOutput;
	FVector CachedAvoidanceHeading = FVector::ZeroVector;
	double LastAvoidanceDecisionTime = -1.0;
	bool bNavigationEnabled = true;
};
