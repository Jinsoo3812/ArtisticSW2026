// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StorageWindowWidget.generated.h"

class ABasePlayer;
class AStorageChest;
class UBorder;
class UStorageEntryWidget;
class UTextBlock;
class UUniformGridPanel;

UCLASS()
class CLASSFEATURE_API UStorageWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void InitializeStorage(AStorageChest* InStorageChest, ABasePlayer* InPlayer);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> StoragePanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StorageTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> StorageGridPanel;

	UPROPERTY(EditDefaultsOnly, Category = "Storage")
	TSubclassOf<UStorageEntryWidget> StorageEntryWidgetClass;

	UPROPERTY()
	TObjectPtr<AStorageChest> CachedStorageChest;

	UPROPERTY()
	TObjectPtr<ABasePlayer> CachedPlayer;

	void BuildWidgetTree();
	void RefreshStorage();
	void HandleStorageChanged();
};
