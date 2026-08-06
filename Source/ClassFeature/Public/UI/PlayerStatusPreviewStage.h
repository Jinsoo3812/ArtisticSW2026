#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlayerStatusPreviewStage.generated.h"

class ABasePlayer;
class UImage;
class UMeshComponent;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class USkeletalMeshComponent;
class UTextureRenderTarget2D;

/**
 * Isolated render stage used by the Status window.
 * It copies only the local player's visuals and plays a weapon-specific idle.
 */
UCLASS()
class CLASSFEATURE_API APlayerStatusPreviewStage : public AActor
{
	GENERATED_BODY()

public:
	APlayerStatusPreviewStage();

	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetSourcePlayer(ABasePlayer* InPlayer);
	void RefreshFromPlayer();
	void SetPreviewEnabled(bool bEnabled);
	UTextureRenderTarget2D* GetRenderTarget() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> PreviewAnchor;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> OwnedRenderTarget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> ClonedWeaponMeshes;

	TWeakObjectPtr<ABasePlayer> SourcePlayer;

	void EnsureRenderTarget();
	void CopyPlayerMesh();
	void CopyEquippedWeapon();
	void ClearWeaponMeshes();
	void ApplyPreviewIdle();
	void FramePreview();
	void LogRenderTargetCoverage();
};
