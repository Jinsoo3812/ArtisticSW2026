#include "RecipeIngredientCustomization.h"

#include "Crafting/CraftingRecipeTypes.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Item/ItemData.h"
#include "PropertyHandle.h"
#include "Settings_Item.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TSharedPtr<IPropertyHandle> FindRecipeResult(TSharedRef<IPropertyHandle> Ingredient)
	{
		TSharedPtr<IPropertyHandle> Parent = Ingredient->GetParentHandle();
		for (int32 Depth = 0; Parent.IsValid() && Depth < 5; ++Depth)
		{
			TSharedPtr<IPropertyHandle> Result = Parent->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCraftingRecipeRow, ResultItemTag));
			if (Result.IsValid()) return Result;
			Parent = Parent->GetParentHandle();
		}
		return nullptr;
	}

	FGameplayTag ReadTag(const TSharedPtr<IPropertyHandle>& Handle)
	{
		if (!Handle.IsValid()) return {};
		TArray<void*> Data;
		Handle->AccessRawData(Data);
		return Data.Num() == 1 && Data[0] ? *static_cast<FGameplayTag*>(Data[0]) : FGameplayTag();
	}

	void WriteTag(const TSharedPtr<IPropertyHandle>& Handle, FGameplayTag Tag)
	{
		if (!Handle.IsValid()) return;
		TArray<void*> Data;
		Handle->AccessRawData(Data);
		if (Data.Num() != 1 || !Data[0]) return;
		Handle->NotifyPreChange();
		*static_cast<FGameplayTag*>(Data[0]) = Tag;
		Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
		Handle->NotifyFinishedChangingProperties();
	}
}

TSharedRef<IPropertyTypeCustomization> FRecipeIngredientCustomization::MakeInstance()
{
	return MakeShared<FRecipeIngredientCustomization>();
}

void FRecipeIngredientCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils)
{
	HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()]
		.ValueContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Material and quantity")))];
}

void FRecipeIngredientCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils)
{
	TSharedPtr<IPropertyHandle> TagHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCraftingItemStack, ItemTag));
	TSharedPtr<IPropertyHandle> QuantityHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCraftingItemStack, Quantity));
	TSharedPtr<IPropertyHandle> ResultHandle = FindRecipeResult(StructHandle);
	if (!TagHandle.IsValid() || !QuantityHandle.IsValid()) return;
	if (!ResultHandle.IsValid())
	{
		ChildBuilder.AddProperty(TagHandle.ToSharedRef());
		ChildBuilder.AddProperty(QuantityHandle.ToSharedRef());
		return;
	}
	ChildBuilder.AddCustomRow(FText::FromString(TEXT("Recipe material")))
		.NameContent()[TagHandle->CreatePropertyNameWidget()]
		.ValueContent()[
			SNew(SComboButton)
			.OnGetMenuContent_Lambda([TagHandle, ResultHandle]() -> TSharedRef<SWidget>
			{
				FMenuBuilder Menu(true, nullptr);
				const USettings_Item* Settings = GetDefault<USettings_Item>();
				const UItemData* Definitions = Settings ? Settings->ItemAssetRegistry.LoadSynchronous() : nullptr;
				const TArray<FGameplayTag> Options = Definitions
					? Definitions->GetCraftingMaterialOptions(ReadTag(ResultHandle)) : TArray<FGameplayTag>();
				for (const FGameplayTag Option : Options)
				{
					Menu.AddMenuEntry(FText::FromName(Option.GetTagName()), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([TagHandle, Option]() { WriteTag(TagHandle, Option); })));
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()[SNew(STextBlock).Text_Lambda([TagHandle]()
			{
				const FGameplayTag Tag = ReadTag(TagHandle);
				return Tag.IsValid() ? FText::FromName(Tag.GetTagName()) : FText::FromString(TEXT("Select material"));
			})]
		];
	ChildBuilder.AddProperty(QuantityHandle.ToSharedRef());
}
