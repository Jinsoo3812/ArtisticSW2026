#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipUpgradePreviewStage.generated.h"

struct FStreamableHandle;
class USceneCaptureComponent2D;
class USceneComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class UTextureRenderTarget2D;

/**
 * UI-only preview stage. Blueprint children may add lights/background meshes,
 * but class loading, actor replacement and rotation are native.
 */
UCLASS(Blueprintable)
class SHIPUPGRADEUI_API AShipUpgradePreviewStage : public AActor
{
	GENERATED_BODY()

public:
	AShipUpgradePreviewStage();
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void SetPreviewActorSoftClass(TSoftClassPtr<AActor> InActorClass);

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void SetPreviewActorClass(TSubclassOf<AActor> InActorClass);

	/**
	 * Copies the currently visible mesh components from an existing ship into the
	 * isolated preview stage. No gameplay actor, physics state, or collision is duplicated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void SetPreviewSourceActor(AActor* InSourceActor);

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void ClearPreviewActor();

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void AddPreviewYaw(float DeltaYaw);

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade|Preview")
	UTextureRenderTarget2D* GetRenderTarget() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<USceneComponent> PreviewAnchor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<AActor> SpawnedPreviewActor;

	/**
	 * Multiplies the automatically calculated camera distance.
	 * Smaller values make the preview fill more of the frame.
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Preview|Framing",
		meta = (ClampMin = "0.5", ClampMax = "3.0", UIMin = "0.5", UIMax = "2.0"))
	float FrameDistanceMultiplier = 0.9f;

private:
	void EnsureOwnedRenderTarget();
	void ClearClonedPreviewComponents();
	void CloneVisibleMeshes(AActor* SourceRoot, AActor* ActorToCopy);
	void FramePreview();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> OwnedRenderTarget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> ClonedPreviewComponents;

	FSoftObjectPath PendingActorClassPath;
	TSharedPtr<FStreamableHandle> ActorClassLoadHandle;
};
