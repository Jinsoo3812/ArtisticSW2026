// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/StorageWindowWidget.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "UI/StorageEntryWidget.h"

void UStorageWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildWidgetTree();
	RefreshStorage();
}

void UStorageWindowWidget::NativeDestruct()
{
	if (CachedStorageChest)
	{
		if (UStorageComponent* StorageComponent = CachedStorageChest->GetStorageComponent())
		{
			StorageComponent->OnStorageChanged.RemoveAll(this);
		}
	}

	Super::NativeDestruct();
}

void UStorageWindowWidget::InitializeStorage(AStorageChest* InStorageChest, ABasePlayer* InPlayer)
{
	if (CachedStorageChest)
	{
		if (UStorageComponent* OldStorageComponent = CachedStorageChest->GetStorageComponent())
		{
			OldStorageComponent->OnStorageChanged.RemoveAll(this);
		}
	}

	CachedStorageChest = InStorageChest;
	CachedPlayer = InPlayer;

	if (CachedStorageChest)
	{
		if (UStorageComponent* StorageComponent = CachedStorageChest->GetStorageComponent())
		{
			StorageComponent->OnStorageChanged.AddUObject(this, &UStorageWindowWidget::HandleStorageChanged);
		}
	}

	BuildWidgetTree();
	RefreshStorage();
}

void UStorageWindowWidget::BuildWidgetTree()
{
	if (StorageGridPanel || !WidgetTree)
	{
		return;
	}

	USizeBox* RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("StorageRootSizeBox"));
	RootSizeBox->SetWidthOverride(360.0f);

	StoragePanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StoragePanel"));
	StoragePanel->SetPadding(FMargin(14.0f));
	StoragePanel->SetBrushColor(FLinearColor(0.018f, 0.021f, 0.025f, 0.94f));

	UVerticalBox* LayoutBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LayoutBox"));

	StorageTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StorageTitleText"));
	StorageTitleText->SetText(FText::FromString(TEXT("Storage")));
	StorageTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* TitleSlot = LayoutBox->AddChildToVerticalBox(StorageTitleText))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	UScrollBox* ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("StorageScrollBox"));
	StorageGridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("StorageGridPanel"));
	ScrollBox->AddChild(StorageGridPanel);

	if (UVerticalBoxSlot* GridSlot = LayoutBox->AddChildToVerticalBox(ScrollBox))
	{
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
	}

	StoragePanel->SetContent(LayoutBox);
	RootSizeBox->SetContent(StoragePanel);

	WidgetTree->RootWidget = RootSizeBox;
}

void UStorageWindowWidget::RefreshStorage()
{
	BuildWidgetTree();

	if (!StorageGridPanel)
	{
		return;
	}

	StorageGridPanel->ClearChildren();

	if (!CachedStorageChest)
	{
		return;
	}

	UStorageComponent* StorageComponent = CachedStorageChest->GetStorageComponent();
	if (!StorageComponent)
	{
		return;
	}

	if (StorageTitleText)
	{
		StorageTitleText->SetText(CachedStorageChest->GetStorageName());
	}

	const TArray<FInventorySlot>& Slots = StorageComponent->GetSlots();
	const int32 SlotCount = StorageComponent->GetSlotCount();
	const int32 Columns = StorageComponent->GetStorageColumns();
	TSubclassOf<UStorageEntryWidget> EntryClass = StorageEntryWidgetClass;
	if (!EntryClass)
	{
		EntryClass = UStorageEntryWidget::StaticClass();
	}

	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		UStorageEntryWidget* EntryWidget = CreateWidget<UStorageEntryWidget>(this, EntryClass);
		if (!EntryWidget)
		{
			continue;
		}

		if (Slots.IsValidIndex(Index) && !Slots[Index].IsEmpty())
		{
			const FInventorySlot& StorageSlot = Slots[Index];
			const ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer());
			const bool bIsRevealed = !PlayerController || PlayerController->IsStorageSlotRevealed(CachedStorageChest, Index);

			if (bIsRevealed)
			{
				EntryWidget->SetupFromData(
					StorageComponent->GetItemName(StorageSlot.ItemTag),
					StorageSlot.Count,
					StorageComponent->GetItemIcon(StorageSlot.ItemTag),
					Index,
					CachedStorageChest
				);
			}
			else if (PlayerController && PlayerController->IsStorageSlotSearching(CachedStorageChest, Index))
			{
				EntryWidget->SetupAsSearching(Index, CachedStorageChest, SearchIconTexture);
			}
			else
			{
				EntryWidget->SetupAsUnrevealed(Index, CachedStorageChest);
			}
		}
		else
		{
			EntryWidget->SetupAsEmpty(Index, CachedStorageChest);
		}

		if (UUniformGridSlot* GridSlot = StorageGridPanel->AddChildToUniformGrid(EntryWidget, Index / Columns, Index % Columns))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UStorageWindowWidget::HandleStorageChanged()
{
	RefreshStorage();
}
