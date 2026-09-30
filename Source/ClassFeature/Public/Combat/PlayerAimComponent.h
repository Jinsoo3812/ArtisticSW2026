#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Components/ActorComponent.h"
#include "PlayerAimComponent.generated.h"

struct FGameplayEventData;

/** Release input identifies a shot; the camera is sampled later, at the actual release. */
USTRUCT()
struct CLASSFEATURE_API FGameplayAbilityTargetData_ProjectileShot : public FGameplayAbilityTargetData
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid ShotId;
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_ProjectileShot> : TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_ProjectileShot>
{
	enum { WithNetSerializer = true, WithCopy = true };
};

USTRUCT()
struct CLASSFEATURE_API FPlayerShotView
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid ShotId;
	UPROPERTY()
	FVector_NetQuantize10 Origin = FVector::ZeroVector;
	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
	UPROPERTY()
	double SampleServerTime = 0.0;
};

enum class EPlayerShotAimResult : uint8 { Pending, Ready, Rejected };
DECLARE_DELEGATE_RetVal(bool, FPlayerAimObstructionQuery);

/** Camera intent/transport only. No boat rotation, weapon speed, damage or spawning. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UPlayerAimComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPlayerAimComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	void SetObstructionQuery(FPlayerAimObstructionQuery Query);
	bool ResolveCurrentAim(const AActor* Weapon, FVector& OutTarget, FVector& OutDirection, double& OutTime) const;
	bool CreateReleaseRequest(FGameplayEventData& EventData) const;
	bool BeginShot(const FGameplayEventData& EventData, FGuid& OutShotId);
	void EndShot(const FGuid& ShotId);
	void CompleteShot(const FGuid& ShotId, bool bSucceeded);
	bool TryGetShotResolution(const FGuid& ShotId, bool& OutSucceeded) const;
	/** Called by the late release phase on the owning player. Captures/sends at most once. */
	bool CaptureShotView(const FGuid& ShotId);
	EPlayerShotAimResult ResolveShotAim(const FGuid& ShotId, const AActor* Weapon,
		FVector& OutTarget, FVector& OutViewDirection, double& OutAimTime) const;
	/** UI feedback for the last release; the collision resolver remains authoritative. */
	void ReportShotObstruction(bool bBlocked);
	UFUNCTION(BlueprintPure, Category="Combat|Aim")
	bool IsShotObstructed() const;

protected:
	UFUNCTION(Server, Reliable)
	void ServerSubmitShotView(const FPlayerShotView& View);
	UFUNCTION(Client, Reliable)
	void ClientResolveShot(const FGuid& ShotId, bool bSucceeded);
	bool ValidateView(const FPlayerShotView& View) const;
	void AcceptView(const FPlayerShotView& View);
	bool CaptureCurrentView(FPlayerShotView& OutView) const;
	void TraceView(const FPlayerShotView& View, const AActor* Weapon, FVector& OutTarget, FVector& OutDirection) const;

	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0.1"))
	float MaxShotViewAge = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0"))
	float MaxFutureViewTime = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category="Aim", meta=(ClampMin="100"))
	float TraceDistance = 10000.f;
	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0"))
	float MaxViewOriginDistance = 1500.f;
	UPROPERTY(EditDefaultsOnly, Category="Aim|Validation", meta=(ClampMin="0", ClampMax="89"))
	float MaxViewAngleDegrees = 80.f;

private:
	FGuid ActiveShotId;
	FPlayerShotView ShotView;
	bool bHasView = false;
	bool bViewRejected = false;
	bool bShotResolved = false;
	bool bShotSucceeded = false;
	double ObstructionUntil = 0.0;
	FPlayerAimObstructionQuery ObstructionQuery;
	bool bPreviewObstructed = false;
	TArray<FGuid> RecentShotIds;
};
