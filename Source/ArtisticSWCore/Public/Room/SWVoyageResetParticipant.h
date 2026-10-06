#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Room/SWVoyageResetTypes.h"
#include "SWVoyageResetParticipant.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class ARTISTICSWCORE_API USWVoyageResetParticipant : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API ISWVoyageResetParticipant
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyagePolicy GetVoyagePolicy() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyageRestoreStage GetVoyageRestoreStage() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") FName GetVoyageParticipantId() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") TArray<FName> GetVoyageAfterParticipants() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyageStepResult PrepareVoyageReset(const FSWVoyageResetContext& Context, FString& OutError);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyageStepResult ResetVoyageTransientState(const FSWVoyageResetContext& Context, FString& OutError);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyageStepResult RestoreVoyageState(const FSWVoyageResetContext& Context, FString& OutError);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") ESWVoyageStepResult IsVoyageReady(const FSWVoyageResetContext& Context, FString& OutError);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") void ResumeVoyage(const FSWVoyageResetContext& Context);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voyage") void CancelVoyagePreparation(const FSWVoyageResetContext& Context);
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const;
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const;
	virtual FName GetVoyageParticipantId_Implementation() const;
	virtual TArray<FName> GetVoyageAfterParticipants_Implementation() const;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError);
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError);
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError);
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError);
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context);
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context);
};
