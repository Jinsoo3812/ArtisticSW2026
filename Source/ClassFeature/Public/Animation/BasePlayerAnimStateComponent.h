#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BasePlayerAnimStateComponent.generated.h"

class ABasePlayer;

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UBasePlayerAnimStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBasePlayerAnimStateComponent();

	virtual void BeginPlay() override;

	void UpdateAnimationState(float DeltaTime);
	void SetMoveInput(float Right, float Forward);
	void ClearMoveInput();
	void HandleJumpStarted();
	void HandleLanded(const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement|Start")
	void MarkGroundStartFinished();

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement")
	void FinishFallOffStart();

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement")
	void FinishJumpStart();

protected:
	void CacheOwner();
	bool IsInAirForAnimation() const;
	bool ShouldUseLocalInput() const;
	FVector2D GetMovementInputForState() const;
	void UpdateAirState(float DeltaTime);
	void UpdateMovementRequestState(float DeltaTime);
	void UpdateCombatMovementState();
	void UpdateMaxWalkSpeed() const;
	void ClearMovementRequests();
	void StartFallOffStart();
	void StopFallOffStart();
	void StartLanding(float ImpactFallSpeed, bool bTriggerRealLandEvent);
	void FinishLanding();
	void FinishLandingRequest();

protected:
	UPROPERTY(Transient)
	TObjectPtr<ABasePlayer> CachedBasePlayer;

	FTimerHandle JumpStartTimerHandle;
	FTimerHandle FallOffStartTimerHandle;
	FTimerHandle LandingTimerHandle;
	FTimerHandle LandingRequestTimerHandle;

	bool bWasInAir = false;
	bool bSuppressFallOffStart = false;

public:
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsPhysicallyInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsJumping = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsFallOffStart = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsLanding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bLandingRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bCanEnterLand = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bCanEnterGround = true;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float VerticalSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	float MoveInputSize = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Input")
	float MoveInputDeadZone = 0.1f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	FVector2D CachedMoveInput = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	float MoveInputHeldTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float StartToLoopDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float MinStartDatabaseTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float SprintStartToLoopDelay = 0.34f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	float CurrentStartToLoopDelay = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bUseStartDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bGroundStartFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bPendingGroundStartFinish = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bStartWasSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bUseLoopDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bUseSharpTurnDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStartRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStopRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float StopIntentSpeedThreshold = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float IdleSpeedThreshold = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float RunToSprintSpeedThreshold = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	float MoveInputTurnAngle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bSharpTurnRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float SharpTurnAngleThreshold = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float MoveInputTurnDeadZoneAngle = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float SharpTurnMinSpeed = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float JumpStartDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float FallOffStartDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float LandingDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float LandingRequestDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float JumpStartMaxDuration = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LastFallSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LandStartGroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LandStartFallSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bLandWasMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bLandWasSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bUseHeavyLand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float HeavyLandSpeedThreshold = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float RealLandingEventSpeedThreshold = 300.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float WalkSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float SprintSpeed = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float WalkRotationRateYaw = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float SprintRotationRateYaw = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float MovementDirection = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputForward = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputRight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatForwardSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatRightSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float GenericMoveInputSpeedThreshold = 3.f;
};
