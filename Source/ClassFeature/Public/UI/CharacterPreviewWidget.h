#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CharacterPreviewWidget.generated.h"

class ABasePlayer;
class ASceneCapture2D;
class UImage;
class UTextureRenderTarget2D;

/** Renders the live player and equipped item through SceneCapture2D into a UMG image. */
UCLASS()
class CLASSFEATURE_API UCharacterPreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForPlayer(ABasePlayer* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Character Preview")
	void RefreshPreview();

	UFUNCTION(BlueprintCallable, Category = "Character Preview")
	void SetPreviewActive(bool bActive);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CharacterPreviewImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Preview|Capture")
	FVector CameraRelativeLocation = FVector(250.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Preview|Capture")
	FRotator CameraRelativeRotation = FRotator(0.0f, 180.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Preview|Capture", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float CameraFieldOfView = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Preview|Capture", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 RenderTargetResolution = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Preview")
	FLinearColor BackgroundColor = FLinearColor(0.015f, 0.02f, 0.03f, 1.0f);

	TWeakObjectPtr<ABasePlayer> CachedPlayer;

	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> PreviewCaptureActor;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PreviewRenderTarget;

	bool bPreviewActive = false;

	void CreateCaptureResources();
	void DestroyCaptureResources();
};
