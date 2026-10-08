#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWLoadingScreenWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

USTRUCT(BlueprintType)
struct FSWLoadingDestination
{
	GENERATED_BODY()

	// Soft reference: selecting a map must not load the destination world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loading")
	TSoftObjectPtr<UWorld> Level;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loading")
	TObjectPtr<UTexture2D> Background = nullptr;

	// Empty uses the text authored on LoadingMessageText in the Designer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loading", meta=(MultiLine=true))
	FText Message;
};

/** Designer owns layout; native code only fills named image/text widgets. */
UCLASS()
class ARTISTICSW2026_API USWLoadingScreenWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetDestination(const FString& MapName);
	void SetStatus(const FText& Status);
	static FString NormalizeMapName(const FString& MapName);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loading")
	TArray<FSWLoadingDestination> Destinations;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="Loading")
	TObjectPtr<UImage> BackgroundImage;

	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="Loading")
	TObjectPtr<UTextBlock> LoadingMessageText;

	// Optional separate label for Connecting / Preparing player / Returning.
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Loading")
	TObjectPtr<UTextBlock> LoadingStatusText;

private:
	UPROPERTY(Transient)
	FSlateBrush DefaultBackground;
	FText DefaultMessage;
};
