#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "SWGameplayCueNotify_BurstFeedback.generated.h"

class UCameraShakeBase;
class UNiagaraSystem;
class USoundBase;

UENUM(BlueprintType)
enum class ESWGameplayCueCameraShakeRecipient : uint8
{
	AllLocalPlayersInRadius UMETA(DisplayName = "All Local Players In Radius"),
	InstigatorLocalPlayer UMETA(DisplayName = "Instigator Local Player"),
	TargetLocalPlayer UMETA(DisplayName = "Target Local Player")
};

/**
 * Multiplayer-safe one-shot presentation. The server emits the GameplayCue;
 * every receiving machine spawns cosmetic FX locally, while camera shake is
 * restricted to the appropriate local player controller.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "SW Gameplay Cue Burst Feedback"))
class ARTISTICSWCORE_API USWGameplayCueNotify_BurstFeedback : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	USWGameplayCueNotify_BurstFeedback();

	UNiagaraSystem* GetNiagaraSystem() const { return NiagaraSystem; }
	float GetCameraShakeScale() const { return CameraShakeScale; }
	ESWGameplayCueCameraShakeRecipient GetCameraShakeRecipient() const { return CameraShakeRecipient; }

protected:
	virtual bool OnExecute_Implementation(
		AActor* MyTarget,
		const FGameplayCueParameters& Parameters) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|VFX")
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Audio")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera")
	ESWGameplayCueCameraShakeRecipient CameraShakeRecipient =
		ESWGameplayCueCameraShakeRecipient::AllLocalPlayersInRadius;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float CameraShakeInnerRadius = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float CameraShakeOuterRadius = 2000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|Camera", meta = (ClampMin = "0.01"))
	float CameraShakeFalloff = 1.0f;

private:
	static FVector ResolveFeedbackLocation(
		AActor* MyTarget,
		const FGameplayCueParameters& Parameters);
	bool ShouldShakeController(
		const class APlayerController& PlayerController,
		AActor* MyTarget,
		const FGameplayCueParameters& Parameters) const;
	float CalculateCameraShakeScale(
		const class APlayerCameraManager& CameraManager,
		const FVector& Epicenter) const;
};
