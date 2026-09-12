// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StorageWindowWidget.generated.h"

class UInventoryPanelWidget;
class ABasePlayer;
class AStorageChest;
class UBorder;
class UStorageEntryWidget;
class UTextBlock;
class UTexture2D;
class UUniformGridPanel;

UCLASS()
class CLASSFEATURE_API UStorageWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void InitializeStorage(AStorageChest* InStorageChest, ABasePlayer* InPlayer);
	void RefreshStorage();
	void UseInventoryPanel(TSubclassOf<UInventoryPanelWidget> PanelClass);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> StoragePanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StorageTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> StorageGridPanel;

	UPROPERTY(EditDefaultsOnly, Category = "Storage")
	TSubclassOf<UStorageEntryWidget> StorageEntryWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search")
	TObjectPtr<UTexture2D> SearchIconTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search")
	TObjectPtr<UTexture2D> UnrevealedOverlayTexture;

	UPROPERTY()
	TObjectPtr<AStorageChest> CachedStorageChest;

	UPROPERTY()
	TObjectPtr<ABasePlayer> CachedPlayer;

	/** Place the existing inventory WBP with this name to author the shared window. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UInventoryPanelWidget> SharedInventoryPanel;

	void BuildWidgetTree();
	void HandleStorageChanged();
};
