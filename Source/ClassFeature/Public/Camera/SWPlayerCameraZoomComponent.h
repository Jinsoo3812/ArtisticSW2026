#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWPlayerCameraZoomComponent.generated.h"

class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** Local view preference, independent of aim/sprint distances and replicated movement. */
UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USWPlayerCameraZoomComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USWPlayerCameraZoomComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Manual Zoom", meta = (ClampMin = "10", Units = "cm"))
	float MinDistance = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Manual Zoom", meta = (ClampMin = "10", Units = "cm"))
	float MaxDistance = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Manual Zoom", meta = (ClampMin = "1", Units = "cm"))
	float WheelStep = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Manual Zoom", meta = (ClampMin = "0.1"))
	float InterpSpeed = 10.f;
	/** Enable only to opt back into automatic camera retraction. Applied after BP defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Manual Zoom")
	bool bEnableCameraCollision = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Camera|Manual Zoom")
	float PreferredDistance = 400.f;

	void InitializeDistance(float DefaultDistance);
	void BindInput(UEnhancedInputComponent* Input);
	void RestoreDistance(float SavedDistance);
	UFUNCTION(BlueprintCallable, Category = "Camera|Manual Zoom")
	void SetPreferredDistance(float Distance);
	/** Positive wheel input zooms in. Suppressed input is discarded, never queued. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Manual Zoom")
	bool ApplyWheelInput(float Axis);
	/** UI/abilities can temporarily own the wheel. Multiple owners may overlap. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Manual Zoom")
	void ReserveWheel(UObject* Consumer, bool bReserve);
	float GetDistance() const;
	float GetSprintDistance(float DefaultDistance, float SprintDistance) const;
	float ClampDistance(float Distance) const;
	float GetSafeInterpSpeed() const;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void HandleWheel(const FInputActionValue& Value);
	bool bInitialized = false;
	TSet<TWeakObjectPtr<UObject>> WheelConsumers;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ZoomContext;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundSubsystem;
	TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
	uint32 BindingHandle = 0;
};
