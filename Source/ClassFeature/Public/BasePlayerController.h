// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BasePlayerController.generated.h"

/**
 * 
 */

class UInputMappingContext;
class UPlayerHUDWidget;
class UInputAction;

UCLASS()
class CLASSFEATURE_API ABasePlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;


	/*---- Input Action ----*/
	// 인벤토리 열기 / 닫기 IA (I)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleInventoryAction;


	/*---- 인벤토리 ----*/
public:

	void ToggleInventory();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPlayerHUDWidget> PlayerHUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UPlayerHUDWidget> PlayerHUDWidget;

	void BindHUDToCurrentPlayer();
	void ApplyInventoryInputMode(bool bOpen);
	
};
