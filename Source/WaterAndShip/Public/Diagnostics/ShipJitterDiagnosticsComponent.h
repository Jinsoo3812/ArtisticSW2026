#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipJitterDiagnosticsComponent.generated.h"

class AShip;
struct FShipRenderProbeState;
class FShipRenderProbeViewExtension;

UCLASS(ClassGroup=(Diagnostics), meta=(BlueprintSpawnableComponent))
class WATERANDSHIP_API UShipJitterDiagnosticsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipJitterDiagnosticsComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void WriteCsvLine(const FString& Line);
	void WriteRenderCsvLine(const FString& Line);
	void CloseCsv();

	TUniquePtr<FArchive> CsvArchive;
	TUniquePtr<FArchive> RenderCsvArchive;
	TSharedPtr<FShipRenderProbeState, ESPMode::ThreadSafe> RenderProbeState;
	TSharedPtr<FShipRenderProbeViewExtension, ESPMode::ThreadSafe> RenderViewExtension;
	TWeakObjectPtr<AShip> CachedShip;
	FTransform PreviousRootTransform = FTransform::Identity;
	FTransform PreviousCameraTransform = FTransform::Identity;
	FTransform PreviousRelativeTransform = FTransform::Identity;
	FVector2D PreviousScreenPosition = FVector2D::ZeroVector;
	FVector MarkerLocalPosition = FVector::ZeroVector;
	double NextSummaryTime = 0.0;
	uint64 PreviousCorrectionSerial = 0;
	uint64 PreviousRenderRequestFrame = 0;
	uint64 PreviousRenderFrame = 0;
	FTransform PreviousRequestedVisualTransform = FTransform::Identity;
	FTransform PreviousRenderProxyTransform = FTransform::Identity;
	bool bHasPreviousRenderSample = false;
	bool bHasPreviousSample = false;
	bool bAutoCamera = false;
	bool bHasAutoCameraBaseRotation = false;
	FRotator AutoCameraBaseRotation = FRotator::ZeroRotator;
};
