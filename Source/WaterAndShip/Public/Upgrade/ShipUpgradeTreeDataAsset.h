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
