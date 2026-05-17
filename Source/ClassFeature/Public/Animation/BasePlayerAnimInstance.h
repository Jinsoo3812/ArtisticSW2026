// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BasePlayerAnimInstance.generated.h"

class ABasePlayer;
class ACharacter;
class APawn;

UCLASS(Blueprintable, BlueprintType)
class CLASSFEATURE_API UBasePlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void CacheOwningCharacter();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void ResetAnimationState();

protected:
	void UpdateFromGenericCharacter(float DeltaSeconds);
	void UpdateFromPlayerCharacter(float DeltaSeconds, const ABasePlayer& PlayerCharacter);
	void UpdateAimOffset();
	float CalculateAimOffsetAlpha() const;

protected:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<APawn> CachedPawn;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<ACharacter> CachedCharacter;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<ABasePlayer> CachedBasePlayer;

public:
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float DeltaTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float VerticalSpeed = 0.f;

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

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	float MoveInputSize = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	float MoveInputHeldTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	float CurrentStartToLoopDelay = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStartRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStopRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bUseStartDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bUseLoopDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bUseSharpTurnDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	float MoveInputTurnAngle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bSharpTurnRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float StopIntentSpeedThreshold = 80.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float IdleSpeedThreshold = 30.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float RunToSprintSpeedThreshold = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float SharpTurnAngleThreshold = 60.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float MoveInputTurnDeadZoneAngle = 5.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Tuning")
	float SharpTurnMinSpeed = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsCombatMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsDodging = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsHitReacting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsPlayingCombatIntro = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bPendingCombatModeFromIntro = false;

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

	UPROPERTY(BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimOffsetAlpha = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	bool bForceAimOffsetAlwaysOn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float MaxAimYaw = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float MaxAimPitch = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float StandingAimAlpha = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float MovingAimAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float SprintAimAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|AimOffset")
	float CombatAimAlpha = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float GenericMoveInputSpeedThreshold = 3.f;
};
