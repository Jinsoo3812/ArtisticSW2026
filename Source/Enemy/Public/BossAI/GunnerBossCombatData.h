#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GunnerBossCombatData.generated.h"

class UAnimMontage;

/** Asset-free client ray presentation. Thickness is in screen pixels. */
USTRUCT(BlueprintType)
struct ENEMY_API FGunnerBossRaySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray") FLinearColor Color = FLinearColor::Red;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (ClampMin = "0.1")) float Thickness = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (ClampMin = "0.1")) float ShotThickness = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (ClampMin = "0.01", Units = "s")) float ShotLifetime = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (ClampMin = "0.01", Units = "s")) float EndPresentationLifetime = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (ClampMin = "0.0")) float EndpointInterpolationSpeed = 20.f;
	bool IsValid() const;
};

/** All times use the server clock; LockTime is included in PrepareTime. */
USTRUCT(BlueprintType)
struct ENEMY_API FGunnerBossShotPattern
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.01", Units = "s"))
	float PrepareTime = 2.f;
	/** Zero tracks the player until the firing boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float LockTime = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float RecoveryTime = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float ShotGap = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float Cooldown = 0.f;
	/** Shared attack admission delay after recovery; movement is allowed during this delay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float PostAttackGap = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.001"))
	float DamageCoefficient = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.01"))
	float MontagePlayRate = 1.f;
	/** Optional in-place looping sections. Timers, not notifies, advance gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation") FName PrepareSection = TEXT("Aim");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation") FName FireSection = TEXT("Fire");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation") FName RecoverySection = TEXT("Recover");
	bool IsValid() const;
};

/** Shared gun settings; no weapon/projectile data or BT copies of attack timing are required. */
UCLASS(BlueprintType)
class ENEMY_API UGunnerBossCombatData : public UDataAsset
{
	GENERATED_BODY()
public:
	UGunnerBossCombatData();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun") FName FireSocketName = TEXT("Gun_Muzzle");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun", meta = (ClampMin = "1.0", Units = "cm"))
	float AttackRange = 1600.f;
	/** Offset in the target actor's local space (actor origin normally already lies at capsule centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun", meta = (Units = "cm"))
	FVector TargetAimOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun") bool bTraceComplex = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gun", meta = (ClampMin = "0.01", Units = "s"))
	float AimUpdateInterval = 0.05f;
	/** Red ray is drawn locally on every client without a mesh, material or Niagara asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation") FGunnerBossRaySettings Ray;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (Categories = "GameplayCue.Impact")) FGameplayTag ImpactCue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attacks") FGunnerBossShotPattern Basic;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attacks") FGunnerBossShotPattern Burst;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attacks", meta = (ClampMin = "1", ClampMax = "16"))
	int32 BurstShotCount = 3;
	bool Validate(FString& OutError) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
