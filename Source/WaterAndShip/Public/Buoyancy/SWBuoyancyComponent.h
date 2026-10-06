#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Water/SWBuoyancyTypes.h"
#include "SWBuoyancyComponent.generated.h"

class UBuoyancyComponent;
class UPrimitiveComponent;
class UWaterBodyComponent;
class FChestLaunchPhysicsDiagnostic;
class FSWPhysicsStepBuoyancy;

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
	FString WaterBodyName;
	float DampingForce = 0.0f;
	FString SimulatingComponentName;
	double WorldTimeSeconds = 0.0;
	double ServerTimeSeconds = 0.0;
	float WaveReferenceTime = 0.0f;
	float EffectiveWaveTime = 0.0f;
	FString WaveClass;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetComponentTickEnabled(bool bEnabled) override;
	virtual void OnUnregister() override;
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

	/** Native chest opt-in: retain bounded history and log only anomalous upward motion. */
	bool bMonitorChestLaunch = false;

	/** Native opt-in: GT queries water; authority Chaos steps recompute and apply forces. */
	bool bUsePhysicsStepBuoyancy = false;

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
	void StopPhysicsStepBuoyancy();
	FSWPhysicsStepBuoyancy* PhysicsStepBuoyancy = nullptr;
	uint32 PhysicsStepBuoyancySequence = 0;
	bool ShouldApplyForces() const;
	bool QueryWaterSurface(const FVector& Position, float& OutWaterHeight, FVector& OutWaterVelocity, FString& OutWaterBodyName, const UWaterBodyComponent*& OutWaterBody) const;
	UPrimitiveComponent* ResolveSimulatingComponent() const;
	void MonitorChestLaunch(UPrimitiveComponent* Body, float DeltaTime);
	void UpdateChestPhysicsDiagnostic(UPrimitiveComponent* Body);
	FChestLaunchPhysicsDiagnostic* ChestPhysicsDiagnostic = nullptr;
	uint32 ChestDiagnosticSequence = 0;
	uint32 ChestDiagnosticRequest = 0;
	struct FChestLaunchSample
	{
		FSWBuoyancyRuntimeDiagnostic Solve;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float DeltaTime = 0.0f;
	};
	FChestLaunchSample PreviousLaunchSample;
	FChestLaunchSample LaunchHistory[8];
	int32 LaunchHistoryCount = 0;
	int32 LaunchHistoryNext = 0;
	uint32 PreviousLaunchReasons = 0;
	bool bHasPreviousLaunchSample = false;
	double NextLaunchLogTime = 0.0;
	double NextLaunchHistoryTime = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWaterBodyComponent>> WaterBodies;

	bool bCommandLineDiagnostics = false;
	bool bUsingLegacyFallback = false;
	float NextDiagnosticTime = 0.0f;
	FSWBuoyancyRuntimeDiagnostic LastRuntimeDiagnostic;
};
