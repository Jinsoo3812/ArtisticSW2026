#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Water/SWBuoyancyTypes.h"
#include "SWBuoyancyComponent.generated.h"

class UBuoyancyComponent;
class UPrimitiveComponent;
class UWaterBodyComponent;

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
 * Player buoyancy remains in SwimmingComponent and reuses FSWBuoyancyMath only.
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SW Buoyancy|Debug")
	bool bDrawDebugPontoons = false;

private:
	bool ShouldApplyForces() const;
	bool QueryWaterSurface(const FVector& Position, float& OutWaterHeight, FVector& OutWaterVelocity) const;
	UPrimitiveComponent* ResolveSimulatingComponent() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWaterBodyComponent>> WaterBodies;

	bool bCommandLineDiagnostics = false;
	bool bUsingLegacyFallback = false;
	float NextDiagnosticTime = 0.0f;
};
