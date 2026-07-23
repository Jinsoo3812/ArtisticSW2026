#pragma once

#include "CoreMinimal.h"
#include "UI/FacilityHubWidget.h"
#include "ShipUpgradeWorkspaceWidget.generated.h"

/**
 * Compatibility parent for the existing WBP_WorkspaceScreen design.
 * All top-level behavior now comes from the common FacilityHub shell.
 */
UCLASS(Abstract, BlueprintType)
class SHIPUPGRADEUI_API UShipUpgradeWorkspaceWidget : public UFacilityHubWidget
{
	GENERATED_BODY()
};
