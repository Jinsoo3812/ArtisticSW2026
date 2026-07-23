#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipUpgradePreviewStage.generated.h"

struct FStreamableHandle;
class USceneCaptureComponent2D;
class USceneComponent;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void SetPreviewActorSoftClass(TSoftClassPtr<AActor> InActorClass);

	UFUNCTION(BlueprintCallable, Category = "Ship Upgrade|Preview")
	void SetPreviewActorClass(TSubclassOf<AActor> InActorClass);

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

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<AActor> SpawnedPreviewActor;

private:
	FSoftObjectPath PendingActorClassPath;
	TSharedPtr<FStreamableHandle> ActorClassLoadHandle;
};
