#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bombardment.generated.h"

class ACannonball;
class AShip;
class UMaterialInterface;
class UMeshComponent;
class USceneComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EBombardmentDistributionMode : uint8
{
	StratifiedDisk,
	PureRandom
};

struct FBombardmentMeshHighlightState
{
	TWeakObjectPtr<UMeshComponent> Mesh;
	TWeakObjectPtr<UMaterialInterface> OverlayMaterial;
	TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
	bool bMaterialsReplaced = false;
	bool bRenderedCustomDepth = false;
	int32 CustomDepthStencilValue = 0;
};

/** Local-only targeting visualization. Its mesh/material are intended to be authored in a Blueprint child. */
UCLASS(Blueprintable)
class WATERANDSHIP_API ABombardmentPreview : public AActor
{
	GENERATED_BODY()

public:
	ABombardmentPreview();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ConfigurePreview(float InSkillRadius);
	void SetPreviewValid(bool bInValid);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bombardment|Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	/** Optional material used while the target is valid. Null preserves material slot 0 from the Preview Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Preview")
	TObjectPtr<UMaterialInterface> ValidPreviewMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Preview")
	TObjectPtr<UMaterialInterface> InvalidPreviewMaterial;

	/** Final world-space thickness of the preview mesh after radius scaling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Preview", meta = (ClampMin = "0.01", Units = "cm"))
	float PreviewWorldThickness = 1.0f;

	/** Local-only material applied to enemies inside the preview radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Highlight")
	TObjectPtr<UMaterialInterface> TargetHighlightOverlayMaterial;

	/** Replaces every visible mesh material slot. More reliable than Unreal's optional overlay pass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Highlight")
	bool bReplaceTargetMaterials = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Highlight")
	bool bUseCustomDepthHighlight = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Highlight", meta = (ClampMin = "0", ClampMax = "255"))
	int32 TargetHighlightStencilValue = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Highlight", meta = (ClampMin = "0.02", Units = "s"))
	float HighlightRefreshInterval = 0.1f;

protected:
	virtual void BeginPlay() override;

private:
	void RefreshHighlightedTargets();
	void HighlightActor(AActor* Actor);
	void RestoreHighlights();

	float SkillRadius = 0.0f;
	float HighlightRefreshAccumulator = 0.0f;
	bool bPreviewValid = false;
	bool bHasLoggedPreviewValidity = false;
	bool bHasLoggedHighlightDisabledReason = false;
	int32 LastLoggedEnemyShipCount = INDEX_NONE;
	int32 LastLoggedInRangeEnemyShipCount = INDEX_NONE;
	int32 LastLoggedHighlightedMeshCount = INDEX_NONE;
	TSet<FString> LoggedHighlightMeshPaths;
	TWeakObjectPtr<UMaterialInterface> AuthoredPreviewMaterial;
	TArray<FBombardmentMeshHighlightState> HighlightedMeshes;
};

/** Server-only scheduler that spawns the replicated cannonballs for one confirmed skill use. */
UCLASS(Blueprintable)
class WATERANDSHIP_API ABombardment : public AActor
{
	GENERATED_BODY()

public:
	ABombardment();

	virtual void Tick(float DeltaSeconds) override;

	void InitializeBombardment(
		AShip* InSourceShip,
		APawn* InInstigatorPawn,
		const FVector& InTargetCenter,
		TSubclassOf<AActor> InResolvedProjectileClass,
		float InProjectileDamage,
		float InProjectileSpeed);

	/** Creates a somewhat-random but evenly covered disk. PureRandom is available for comparison. */
	static TArray<FVector2D> GenerateDiskOffsets(
		int32 Count,
		float Radius,
		int32 Seed,
		EBombardmentDistributionMode Mode,
		float Jitter);

	/** Returns start-relative shot times. VolleyInterval is measured start-to-start. */
	static TArray<float> BuildShotTimes(
		int32 InProjectilesPerVolley,
		float InBurstSpreadDuration,
		float InVolleyInterval,
		int32 InVolleyCount);

	/** Fixed-speed ballistic solution under constant world gravity. */
	static bool SolveBallisticVelocity(
		const FVector& Start,
		const FVector& Target,
		float Speed,
		float GravityZ,
		FVector& OutVelocity);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Targeting", meta = (ClampMin = "1.0", Units = "cm"))
	float SkillRadius = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Targeting", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxTargetRange = 15000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Targeting", meta = (Units = "cm"))
	float PreviewHeightOffset = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Targeting")
	TSubclassOf<ABombardmentPreview> PreviewClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Projectile", meta = (ClampMin = "1.0", Units = "cm"))
	float LaunchHeightZ = 10000.0f;

	/** World-space XY offset from the impact disk to the launch disk. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Projectile", meta = (Units = "cm"))
	FVector2D LaunchXYOffset = FVector2D(6000.0f, 0.0f);

	/** Optional override. Null reuses the normal cannonball class found on the controlled Player ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Projectile")
	TSubclassOf<AActor> ProjectileClassOverride;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Volley", meta = (ClampMin = "1"))
	int32 ProjectilesPerVolley = 6;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Volley", meta = (ClampMin = "0.0", Units = "s"))
	float BurstSpreadDuration = 0.5f;

	/** Start-to-start delay between projectile groups. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Volley", meta = (ClampMin = "0.01", Units = "s"))
	float VolleyInterval = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Volley", meta = (ClampMin = "1"))
	int32 VolleyCount = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Distribution")
	EBombardmentDistributionMode DistributionMode = EBombardmentDistributionMode::StratifiedDisk;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Distribution", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistributionJitter = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment|Debug")
	bool bDrawDebug = false;

private:
	struct FScheduledShot
	{
		float FireTime = 0.0f;
		FVector2D DiskOffset = FVector2D::ZeroVector;
	};

	void BuildSchedule();
	void FireShot(const FScheduledShot& Shot);

	UPROPERTY()
	TObjectPtr<AShip> SourceShip;

	UPROPERTY()
	TObjectPtr<APawn> SkillInstigator;

	TSubclassOf<AActor> ResolvedProjectileClass;
	FVector TargetCenter = FVector::ZeroVector;
	float ProjectileDamage = 0.0f;
	float ProjectileSpeed = 0.0f;
	float ElapsedSeconds = 0.0f;
	int32 NextShotIndex = 0;
	bool bStarted = false;
	TArray<FScheduledShot> ScheduledShots;
};
