#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "EnemyHealthBarComponent.generated.h"

class UBaseHealthComponent;
class UPrimitiveComponent;

/**
 * Replicates the one-way player-damage reveal state and evaluates viewport/LOS locally.
 * The reveal latch is reset only when a pooled enemy is prepared for reuse.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UEnemyHealthBarComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void ConfigurePresentation(const FVector& RelativeOffset, const FVector2D& InDrawSize);
	void SetVisibilitySourceComponent(UPrimitiveComponent* InVisibilitySourceComponent);

	/** Local presentation gate used by replicated pooling state. */
	void SetOwnerPresentationActive(bool bNewActive);

	/** Authority-only reset for a pooled enemy entering a fresh lifetime. */
	void ResetRevealState();

	UFUNCTION(BlueprintPure, Category = "Enemy Health Bar")
	bool WasRevealedByPlayerDamage() const { return bRevealedByPlayerDamage; }

	UFUNCTION(BlueprintPure, Category = "Enemy Health Bar")
	float GetDeathHideDelay() const { return DeathHideDelay; }

	static bool ResolveShouldDisplay(
		bool bRevealed,
		bool bInViewport,
		bool bHasLineOfSight,
		bool bVisibilitySourcePresented,
		bool bDead,
		bool bPresentationActive);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Health Bar", meta = (ClampMin = "0.02", Units = "s"))
	float VisibilityEvaluationInterval = 0.1f;

	/** 체력이 0이 된 모습을 잠시 보여준 뒤 체력바를 숨기는 시간입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Health Bar", meta = (ClampMin = "0.0", Units = "s"))
	float DeathHideDelay = 0.1f;

	UPROPERTY(ReplicatedUsing = OnRep_RevealedByPlayerDamage, VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Health Bar")
	bool bRevealedByPlayerDamage = false;

private:
	UFUNCTION()
	void HandleHealthChanged(
		UBaseHealthComponent* InHealthComponent,
		float OldValue,
		float NewValue,
		AActor* InstigatorActor);

	UFUNCTION()
	void HandleMaxHealthChanged(
		UBaseHealthComponent* InHealthComponent,
		float OldValue,
		float NewValue,
		AActor* InstigatorActor);

	UFUNCTION()
	void OnRep_RevealedByPlayerDamage();

	void StartDepletedHealthHideDelay();
	void HideAfterDeathDelay();

	void BindHealthComponent();
	void UnbindHealthComponent();
	void RefreshHealth();
	void RefreshLocalVisibility();
	bool IsVisibilitySourcePresented() const;
	bool IsAnchorInLocalViewport(APlayerController* PlayerController) const;
	bool HasLineOfSightFromLocalCamera(APlayerController* PlayerController) const;
	void BuildLineOfSightSamplePoints(const FVector& CameraLocation, TArray<FVector>& OutPoints) const;
	static bool IsPlayerDamageSource(const AActor* SourceActor);

	UPROPERTY(Transient)
	TObjectPtr<UBaseHealthComponent> BoundHealthComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> VisibilitySourceComponent;

	bool bOwnerPresentationActive = true;
	bool bDeathHideDelayActive = false;
	bool bHiddenForDepletedHealth = false;
	FTimerHandle DeathHideTimerHandle;
};
