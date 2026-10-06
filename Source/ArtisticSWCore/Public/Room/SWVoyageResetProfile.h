#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SWVoyageResetProfile.generated.h"

class UWorld;

UCLASS(BlueprintType)
class ARTISTICSWCORE_API USWVoyageResetProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	USWVoyageResetProfile();
	UPROPERTY(EditDefaultsOnly, Category="Voyage") TSoftObjectPtr<UWorld> GameplayLevel;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") float PresentationTimeoutSeconds = 3.f;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") float StreamingTimeoutSeconds = 60.f;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") float RestoreTimeoutSeconds = 30.f;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") float ClientReadyTimeoutSeconds = 30.f;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") float TotalTimeoutSeconds = 120.f;
	UPROPERTY(EditDefaultsOnly, Category="Voyage") TArray<FName> ProjectScriptPackages;
	bool ValidateProfile(FString& OutError) const;
};
