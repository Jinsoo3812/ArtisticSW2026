// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "BasePlayerAnimInstance.generated.h"

class ABasePlayer;
class ACharacter;
class APawn;
class ULocomotionAnimStateComponent;

USTRUCT(BlueprintType)
struct FBasePlayerAimThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimPitch = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimOffsetAlpha = 0.f;
};

USTRUCT(BlueprintType)
struct FBasePlayerAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FBasePlayerAnimInstanceProxy();
	FBasePlayerAnimInstanceProxy(UAnimInstance* InAnimInstance);

	const FBasePlayerAimThreadSafeData& GetThreadSafeAimData() const { return ThreadSafeAimData; }
	void SetThreadSafeAimData(const FBasePlayerAimThreadSafeData& InAimData) { ThreadSafeAimData = InAimData; }
	bool GetThreadSafeStopRequested() const { return bThreadSafeStopRequested; }
	void SetThreadSafeStopRequested(bool bInStopRequested) { bThreadSafeStopRequested = bInStopRequested; }

private:
	UPROPERTY(Transient)
	FBasePlayerAimThreadSafeData ThreadSafeAimData;

	UPROPERTY(Transient)
	bool bThreadSafeStopRequested = false;
};

UCLASS(Blueprintable, BlueprintType)
class CLASSFEATURE_API UBasePlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

	friend struct FBasePlayerAnimInstanceProxy;

public:
	UBasePlayerAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void CacheOwningCharacter();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void ResetAnimationState();

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement|Start")
	void MarkGroundStartFinished();

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimYaw() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimPitch() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimOffsetAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementPlantSettings Get_FootPlacementPlantSettings() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementInterpolationSettings Get_FootPlacementInterpolationSettings() const;

protected:
	void UpdateFromGenericCharacter(float DeltaSeconds);
	void UpdateFromPlayerCharacter(float DeltaSeconds, const ABasePlayer& PlayerCharacter);
	void UpdateFromAnimStateComponent(const ULocomotionAnimStateComponent& AnimState);
	void UpdateAimOffset();
	float CalculateAimOffsetAlpha() const;
	void PublishAimOffsetToProxy();

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

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bGroundStartFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bPendingGroundStartFinish = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bStartWasSprinting = false;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
	FFootPlacementPlantSettings FootPlacementPlantSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
	FFootPlacementPlantSettings FootPlacementPlantSettingsStops;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Foot Placement", AdvancedDisplay)
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsStops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float GenericMoveInputSpeedThreshold = 3.f;
};
