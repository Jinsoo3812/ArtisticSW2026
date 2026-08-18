#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "PlayerStatusPreviewStage.generated.h"

class ABasePlayer;
class ABaseItem;
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
	FGameplayTag GetPreviewWeaponTag() const { return PreviewWeaponTag; }

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

	UPROPERTY(Transient)
	TObjectPtr<ABaseItem> PreviewWeaponActor;

	FGameplayTag PreviewWeaponTag;

	TWeakObjectPtr<ABasePlayer> SourcePlayer;

	void EnsureRenderTarget();
	void CopyPlayerMesh();
	void CopyPreviewWeapon();
	void ClearWeaponMeshes();
	FGameplayTag ResolvePreviewWeaponTag() const;
	ABaseItem* EnsurePreviewWeaponActor();
	void DestroyPreviewWeaponActor();
	void ApplyPreviewIdle();
	void FramePreview();
	void LogRenderTargetCoverage();
};
