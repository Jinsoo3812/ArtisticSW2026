#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ShipUpgradeTreeDataAsset.generated.h"

UCLASS(BlueprintType)
class WATERANDSHIP_API UShipUpgradeTreeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Single source of truth for player base and upgrade target values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Data Table", meta = (RequiredAssetDataTags = "RowStructure=/Script/WaterAndShip.ShipStatRow"))
	TObjectPtr<UDataTable> ShipStatTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade|Data Table")
	FName PlayerBaseRowName = TEXT("PlayerShip");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade")
	FShipStatSnapshot PreviewBaseStats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship Upgrade")
	TArray<FShipUpgradeNodeDefinition> Nodes;

	const FShipUpgradeNodeDefinition* FindNode(FName NodeId) const;
	bool ValidateTree(TArray<FText>& OutErrors) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
