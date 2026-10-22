#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckCombatTargetResolverComponent.generated.h"

class AEnemyShip;

UENUM(BlueprintType)
enum class EDeckTargetAnchorSource : uint8
{
	Unresolved, DeckFloor, SupportedObstacle, AirborneProjection, RecentAnchor
};

/** Tracking geometry is not a walk location and must never be used directly as a movement goal. */
USTRUCT(BlueprintType)
struct ENEMY_API FDeckTargetAnchor
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LocalCenter = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName SurfaceId;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EDeckTargetAnchorSource Source = EDeckTargetAnchorSource::Unresolved;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector ActualWorldLocation = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName FailureReason;
	bool IsValid() const { return !SurfaceId.IsNone() && Source != EDeckTargetAnchorSource::Unresolved; }
	bool HasCurrentEvidence() const { return IsValid() && Source != EDeckTargetAnchorSource::RecentAnchor; }
};

/** Server-only live support/projection query plus short memory for genuinely missing evidence. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckCombatTargetResolverComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDeckCombatTargetResolverComponent();
	bool Resolve(AActor* Target, FDeckTargetAnchor& Out);
	void Reset();
	bool HasExpiredEvidence(AActor* Target) const;
	static bool ResolveFor(const AActor* Observer, AActor* Target, FDeckTargetAnchor& Out);
protected:
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Tracking", meta = (ClampMin = "0", Units = "cm"))
	float MaximumProjectionHeight = 250.f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Tracking", meta = (ClampMin = "0", Units = "cm"))
	float MaximumProjectionDistance = 350.f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Tracking", meta = (ClampMin = "0", Units = "s"))
	float MissingEvidenceGrace = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Tracking") bool bDrawTrackingDebug = false;
private:
	TWeakObjectPtr<AActor> TrackedTarget;
	TWeakObjectPtr<AEnemyShip> TrackedShip;
	FDeckTargetAnchor CachedAnchor;
	FDeckTargetAnchor LastEvidence;
	int32 CachedRevision = INDEX_NONE;
	double LastSampleTime = -1.;
	double LastEvidenceTime = -1.;
	double UnresolvedSince = -1.;
};
