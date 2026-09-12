#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "AreaSlowSkillDataAsset.generated.h"

class AAreaSlowConfirmedDecal;
class AAreaSlowTargetingDecal;
class UGameplayEffect;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FAreaSlowRange
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Range")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Range")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Range")
	FVector BoxExtent = FVector::ZeroVector;

	bool IsValid() const
	{
		return BoxExtent.X > 0.0f && BoxExtent.Y > 0.0f && BoxExtent.Z > 0.0f;
	}
};

/** Designer-authored policy and tuning for the one-shot rectangular slow. */
UCLASS(BlueprintType)
class CLASSFEATURE_API UAreaSlowSkillDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UAreaSlowSkillDataAsset();

	static FAreaSlowRange BuildRangeForTransform(
		const FTransform& SourceTransform,
		float InFrontGap,
		float InRangeLength,
		float InRangeWidth,
		float InRangeHeight,
		float InVerticalOffset);

	FAreaSlowRange BuildRangeForActor(const AActor* SourceActor) const;
	bool IsRuntimeConfigValid(FString* OutFailureReason = nullptr) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Range", meta = (ClampMin = "0.0", Units = "cm"))
	float FrontGap = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Range", meta = (ClampMin = "1.0", Units = "cm"))
	float RangeLength = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Range", meta = (ClampMin = "1.0", Units = "cm"))
	float RangeWidth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Range", meta = (ClampMin = "1.0", Units = "cm"))
	float RangeHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Range", meta = (Units = "cm"))
	float VerticalOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Targeting")
	FGameplayTagQuery RequiredTargetQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Targeting")
	FGameplayTagQuery BlockedTargetQuery;

	/** Collision is the broad phase; gameplay tags remain the final allow-list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Targeting")
	TArray<TEnumAsByte<EObjectTypeQuery>> TargetObjectTypes;

	/** Multiplies the target's final movement speed for SlowDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Effect", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MoveSpeedMultiplier = 0.5f;

	/** Multiplies the target's attack montage speed for the same effect lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Effect", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AttackSpeedMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Effect", meta = (ClampMin = "0.01", Units = "s"))
	float SlowDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Effect")
	TSubclassOf<UGameplayEffect> SlowEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation")
	TSubclassOf<AAreaSlowTargetingDecal> TargetingDecalClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation")
	TSubclassOf<AAreaSlowConfirmedDecal> ConfirmedDecalClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation")
	TObjectPtr<UMaterialInterface> TargetingDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation")
	TObjectPtr<UMaterialInterface> ConfirmedDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation", meta = (ClampMin = "1.0", Units = "cm"))
	float DecalProjectionDepth = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Presentation", meta = (ClampMin = "0.05", Units = "s"))
	float ConfirmedDecalDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Slow|Debug")
	bool bDrawServerDebugBox = false;
};
