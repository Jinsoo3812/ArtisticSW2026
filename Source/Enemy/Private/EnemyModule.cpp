#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "PropertyEditorModule.h"
#include "ShipAI/EnemyShip.h"
#include "Widgets/Input/SButton.h"

namespace
{
	class FEnemyShipDetails final : public IDetailCustomization
	{
	public:
		static TSharedRef<IDetailCustomization> MakeInstance()
		{
			return MakeShared<FEnemyShipDetails>();
		}

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
			DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
			for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
			{
				AEnemyShip* Ship = Cast<AEnemyShip>(Object.Get());
				if (Ship && Ship->HasAnyFlags(RF_ClassDefaultObject))
				{
					BlueprintDefaultShips.Add(Ship);
				}
			}
			if (BlueprintDefaultShips.IsEmpty())
			{
				return;
			}

			IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
				TEXT("Ship|Deck AI|Generation"),
				FText::FromString(TEXT("Deck Waypoint Generation")));
			AddActionButton(Category, TEXT("Generate Deck Waypoints From Deck Mesh"),
				&AEnemyShip::GenerateDeckWaypointsFromDeckMesh);
			AddActionButton(Category, TEXT("Validate Deck Waypoints"),
				&AEnemyShip::ValidateDeckWaypoints);
			AddActionButton(Category, TEXT("Clear Generated Deck Waypoints"),
				&AEnemyShip::ClearGeneratedDeckWaypoints);
		}

	private:
		using FDeckWaypointEditorAction = void (AEnemyShip::*)();

		void AddActionButton(
			IDetailCategoryBuilder& Category,
			const TCHAR* Label,
			FDeckWaypointEditorAction Action)
		{
			const FText ButtonText = FText::FromString(Label);
			Category.AddCustomRow(ButtonText)
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(ButtonText)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([WeakShips = BlueprintDefaultShips, Action]()
				{
					for (const TWeakObjectPtr<AEnemyShip>& WeakShip : WeakShips)
					{
						if (AEnemyShip* Ship = WeakShip.Get())
						{
							(Ship->*Action)();
						}
					}
					return FReply::Handled();
				})
			];
		}

		TArray<TWeakObjectPtr<AEnemyShip>> BlueprintDefaultShips;
	};
}
#endif

class FEnemyModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		FPropertyEditorModule& PropertyEditorModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditorModule.RegisterCustomClassLayout(
			AEnemyShip::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FEnemyShipDetails::MakeInstance));
		PropertyEditorModule.NotifyCustomizationModuleChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (FPropertyEditorModule* PropertyEditorModule =
			FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyEditorModule->UnregisterCustomClassLayout(AEnemyShip::StaticClass()->GetFName());
			PropertyEditorModule->NotifyCustomizationModuleChanged();
		}
#endif
	}
};

IMPLEMENT_MODULE(FEnemyModule, Enemy);
