#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Item/BaseProjectile.h"
#include "Room/SWRoomStateAdapter.h"
#include "ArrowProjectile.generated.h"

USTRUCT()
struct FSWRoomArrowState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) float FlightGravityScale = 0.f;
	UPROPERTY(SaveGame) bool bImpactHandled = false;
	UPROPERTY(SaveGame) bool bDestroyOnImpact = true;
	UPROPERTY(SaveGame) bool bEnableTeamDamageFiltering = true;
	UPROPERTY(SaveGame) FGuid SourceActorId;
	UPROPERTY(SaveGame) FSoftClassPath SourceActorClass;
	UPROPERTY(SaveGame) FSoftClassPath ResolverClass;
	UPROPERTY(SaveGame) TArray<FGuid> IgnoredActorIds;
	UPROPERTY(SaveGame) FSWRoomGameplayEffectState DirectDamageSpec;
	UPROPERTY(SaveGame) TArray<FSWRoomGameplayEffectState> StatusEffectSpecs;
	UPROPERTY(SaveGame) TArray<FGameplayTag> RefreshGrantedTags;
};

class UPrimitiveComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UAbilitySystemComponent;
class UGameplayEffect;
struct FCollisionQueryParams;

/** Minimal transient data required to render an arrow impact on remote clients. */
USTRUCT(BlueprintType)
struct FArrowImpactPresentationData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantize10 ImpactLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantizeNormal IncomingDirection = FVector::ForwardVector;

	/** Set only for a stable replicated moving component, such as a ship query hull. */
	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	TObjectPtr<USceneComponent> AttachComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FName BoneName = NAME_None;
};

USTRUCT(BlueprintType)
struct FArrowStatusEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	TSubclassOf<UGameplayEffect> StatusEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status", meta = (ClampMin = "1"))
	int32 EffectLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	FGameplayTag RefreshGrantedTag;
};

USTRUCT(BlueprintType)
struct FArrowDamageData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "0.0"))
	float AttackCoefficient = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "1"))
	int32 DirectDamageEffectLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	TArray<FArrowStatusEffect> StatusEffects;

};

UCLASS()
class ARTISTICSWCORE_API AArrowProjectile : public ABaseProjectile, public ISWRoomStateAdapter
{
	GENERATED_BODY()

	friend class FStrengthProjectilePayloadTest;

public:
	AArrowProjectile(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override
	{ return FSWRoomStructCodec::Compare<FSWRoomArrowState>(Expected, Actual, TimeToleranceSeconds, OutFields); }
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Consumes final WORLD velocity; no further carrier or direction correction. */
	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void LaunchArrow(const FVector& LaunchVelocity);
	bool LaunchShot(const struct FProjectileShotSnapshot& Shot);

	float GetFlightGravityZ() const;
	float GetFlightGravityScale() const { return FlightGravityScale; }
	virtual FCollisionQueryParams MakeFlightQueryParams() const;
	/** Called only by the movement component after the unified query resolves a contact. */
	virtual void HandleFlightImpact(const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void IgnoreActorForMovement(AActor* ActorToIgnore);

	/** Copies this projectile's authored mesh, materials, and relative transform to a presentation component. */
	bool ApplyVisualTo(UStaticMeshComponent* TargetMesh) const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Visual")
	UStaticMesh* GetArrowVisualMesh() const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Visual")
	FTransform GetArrowVisualRelativeTransform() const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Collision")
	FVector GetCollisionHalfExtent() const { return CollisionHalfExtent.ComponentMax(FVector(0.1f)); }

	UFUNCTION(BlueprintPure, Category = "Arrow|Collision")
	FVector GetObstacleCollisionHalfExtent() const { return ObstacleCollisionHalfExtent.ComponentMax(FVector(0.1f)); }

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	bool InitializeStrengthDamage(
		UAbilitySystemComponent* InSourceASC,
		AActor* InInstigatorActor,
		const FGameplayEffectSpecHandle& InDirectDamageSpec);

	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	float GetAttackCoefficient() const { return DamageData.AttackCoefficient; }

	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	int32 GetDirectDamageEffectLevel() const { return FMath::Max(1, DamageData.DirectDamageEffectLevel); }

	/**
	 * Returns whether this arrow may apply its embedded DamageData to TargetActor.
	 * Team filtering is enabled by default so actors on the same team cannot damage each other.
	 */
	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	bool IsValidDamageTarget(const AActor* TargetActor) const;

	/** Runtime/debug override for testing friendly-fire behavior. */
	UFUNCTION(BlueprintCallable, Category = "Arrow|Debug")
	void SetTeamDamageFilteringEnabled(bool bEnabled) { bEnableTeamDamageFiltering = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Arrow|Debug")
	bool IsTeamDamageFilteringEnabled() const { return bEnableTeamDamageFiltering; }

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Arrow",
		meta = (DeprecatedFunction, DeprecationMessage = "Use the compact impact presentation pipeline."))
	void Multicast_PlayImpactFX(const FHitResult& Hit);

	UFUNCTION(NetMulticast, Unreliable, Category = "Arrow")
	void Multicast_PlayImpactPresentation(const FArrowImpactPresentationData& ImpactData);

protected:
	virtual bool ShouldIgnoreHitActor(const AActor* OtherActor) const;
	virtual bool CanApplyDamageToActor(const AActor* OtherActor) const;
	void ApplyCollisionShape();
	void ApplyArrowCollisionProfile();
	FArrowImpactPresentationData BuildImpactPresentationData(
		UPrimitiveComponent* OtherComp,
		const FHitResult& Hit) const;
	void BuildStatusEffectSpecs();
	bool ApplyDamageToActor(AActor* TargetActor, const FHitResult& HitResult);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arrow")
	void K2_OnImpactFX(const FHitResult& Hit);

protected:
	/** Serialized legacy name retained: existing large BoxComp settings remain character assistance only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Collision",
		meta = (DisplayName = "Character Hit Half Extent", ClampMin = "0.1", UIMin = "0.1", Units = "cm"))
	FVector CollisionHalfExtent = FVector(8.0f, 2.0f, 2.0f);

	/** Small shape for EVERY ship, floor, railing and world obstruction. Independent of hit assistance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Collision",
		meta = (DisplayName = "Obstacle Hit Half Extent", ClampMin = "0.1", UIMin = "0.1", Units = "cm"))
	FVector ObstacleCollisionHalfExtent = FVector(8.0f, 1.0f, 1.0f);

	/** Authoring/visualization shape; movement sweeps it explicitly instead of generating duplicate hit events. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ObstacleCollisionComp;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	FArrowDamageData DamageData;

	FGameplayEffectSpecHandle DirectDamageSpec;

	TArray<FGameplayEffectSpecHandle> StatusEffectSpecHandles;

	UPROPERTY(Transient)
	TArray<FGameplayTag> StatusEffectRefreshGrantedTags;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> SourceASC;

	UPROPERTY(Transient)
	TObjectPtr<AActor> InstigatorActor;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> MovementIgnoredActors;


	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Movement", meta = (ClampMin = "0.0"))
	float FlightGravityScale = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	bool bDestroyOnImpact = true;

	/** Client-only stuck-arrow lifetime. The presentation actor never replicates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Impact", meta = (ClampMin = "0.1", Units = "s"))
	float StuckArrowLifeSpan = 8.0f;

	/** Visual penetration measured forward from the collision box's leading face. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Impact", meta = (ClampMin = "0.0", Units = "cm"))
	float ImpactEmbedDepth = 2.0f;

	bool bImpactHandled = false;

	/**
	 * When enabled, arrows reject targets that share Team.Player or Team.Enemy
	 * with their source. Enabled by default to prevent friendly fire.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Debug")
	bool bEnableTeamDamageFiltering = true;
	FSWRoomArrowState PendingRoomState;
	bool bHasPendingRoomState = false;
};
