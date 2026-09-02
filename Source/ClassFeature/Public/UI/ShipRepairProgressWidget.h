#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipRepairProgressWidget.generated.h"

class SProgressBar;

/** Asset-free repair progress UI. It exists only while the local player holds F. */
UCLASS()
class CLASSFEATURE_API UShipRepairProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRepairProgress(float NormalizedProgress);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SProgressBar> ProgressBar;
};
