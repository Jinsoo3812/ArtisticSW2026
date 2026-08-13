#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Water/SWBuoyancyTypes.h"
#include "SWBuoyancyComponent.generated.h"

class UBuoyancyComponent;
class UPrimitiveComponent;
class UWaterBodyComponent;

/** Last game-thread buoyancy solve, retained so owning actors can emit correlated diagnostics. */
struct WATERANDSHIP_API FSWBuoyancyRuntimeDiagnostic
{
	bool bForceApplicationAllowed = false;
	bool bResolvedSimulatingComponent = false;
	bool bPhysicsSimulationActive = false;
	bool bWaterSurfaceFound = false;
	bool bPontoonInWater = false;
	int32 WaterBodyCount = 0;
	FVector PontoonWorldPosition = FVector::ZeroVector;
	float WaterHeight = -BIG_NUMBER;
	float ImmersionDepth = 0.0f;
	float RelativeVelocityZ = 0.0f;
	float BuoyantForceZ = 0.0f;
	FString SimulatingComponentName;
	double WorldTimeSeconds = 0.0;
};

UENUM(BlueprintType)
enum class ESWBuoyancyExecutionMode : uint8
{
	/** Authority applies forces; clients consume the owner's replicated movement. */
	ServerAuthority UMETA(DisplayName = "Server Authority"),

	/** A separate Network Physics callback consumes this component's data. */
	ExternalNetworkPhysics UMETA(DisplayName = "External Network Physics")
};

/**
 * Shared configuration and game-thread force application for Chaos rigid bodies.
 * Player swimming uses its own CMC custom-movement model and does not apply rigid-body buoyancy forces.
 */
UCLASS(ClassGroup = (Water), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WATERANDSHIP_API USWBuoyancyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWBuoyancyComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "SW Buoyancy")
	void RefreshWaterBodies();

	/** Copies existing Water plugin settings once, allowing current Blueprint assets to migrate safely. */
	UFUNCTION(BlueprintCallable, Category = "SW Buoyancy|Migration")
	bool ImportFromLegacyComponent(UBuoyancyComponent* LegacyComponent, bool bOverwriteExisting = true);

	void ConfigureSinglePontoon(float Radius);

	const TArray<FSWBuoyancyPontoon>& GetPontoons() const { return Pontoons; }
	const FSWBuoyancyForceSettings& GetForceSettings() const { return ForceSettings; }
	ESWBuoyancyExecutionMode GetExecutionMode() const { return ExecutionMode; }
	const FSWBuoyancyRuntimeDiagnostic& GetLastRuntimeDiagnostic() const { return LastRuntimeDiagnostic; }
	int32 GetCachedWaterBodyCount() const { return WaterBodies.Num(); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SW Buoyancy")
	ESWBuoyancyExecutionMode ExecutionMode = ESWBuoyancyExecutionMode::ServerAuthority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SW Buoyancy")
	TArray<FSWBuoyancyPontoon> Pontoons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SW Buoyancy")
	FSWBuoyancyForceSettings ForceSettings;

	/**
	 * Ship migration bridge. Legacy values are imported only while Pontoons is empty.
	 * Once SW pontoons are configured, this component is the authoritative settings source.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SW Buoyancy|Migration",
		meta = (DisplayName = "Import Legacy When SW Pontoons Are Empty"))
	bool bImportLegacyWaterBuoyancy = false;

private:
	bool ShouldApplyForces() const;
	bool QueryWaterSurface(const FVector& Position, float& OutWaterHeight, FVector& OutWaterVelocity) const;
	UPrimitiveComponent* ResolveSimulatingComponent() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWaterBodyComponent>> WaterBodies;

	bool bCommandLineDiagnostics = false;
	bool bUsingLegacyFallback = false;
	float NextDiagnosticTime = 0.0f;
	FSWBuoyancyRuntimeDiagnostic LastRuntimeDiagnostic;
};
