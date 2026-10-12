#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossGunMovementComponent.generated.h"

class AGunnerBossEnemy;

enum class EBossGunMovementResult : uint8 { Ready, Moving, Waiting };

/** Chooses supported deck goals and follows existing routes; it never activates attacks. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UBossGunMovementComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBossGunMovementComponent();
	EBossGunMovementResult UpdatePosition(AActor* Target, float DeltaSeconds);
	void Stop();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "0.0", Units = "cm")) float RetreatEnterDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "1.0", Units = "cm")) float PreferredDistance = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "1.0", Units = "cm")) float ApproachEnterDistance = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "1.0", Units = "cm")) float ReturnBandTolerance = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "1.0", Units = "cm")) float MinimumTravelDistance = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm/s")) float MoveSpeed = 220.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm")) float AcceptanceRadius = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1", Units = "s")) float MaximumMoveTime = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1", Units = "s")) float ProgressTimeout = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm")) float MinimumProgressDistance = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replanning", meta = (ClampMin = "0.05", Units = "s")) float ReplanInterval = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replanning", meta = (ClampMin = "1.0", Units = "cm")) float TargetMoveThreshold = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replanning", meta = (ClampMin = "1")) int32 MaximumCandidateChecks = 24;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replanning", meta = (ClampMin = "0.0")) float TravelScoreWeight = 0.15f;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	bool Plan(AActor* Target, bool bRetreat, bool bApproach);
	void BeginFocus(AActor* Target);
	AGunnerBossEnemy* Boss() const;
	FVector PlannedTargetLocal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PlannedTarget;
	double NextPlanTime = 0.;
	bool bHaveFocus = false;
	bool bSettingsValid = false;
	bool bOldOrient = false;
	bool bOldDesiredRotation = false;
	bool bOldControllerYaw = false;
};
