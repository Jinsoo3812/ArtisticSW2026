#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnemyHealthBarWidget.generated.h"

class UProgressBar;

/** Dedicated enemy-only health presentation used by enemy screen-space widget components. */
UCLASS()
class ENEMY_API UEnemyHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Enemy Health Bar")
	void SetHealthValues(float CurrentHealth, float MaxHealth);

protected:
	/** Widget: ProgressBar / Variable: EnemyHealthProgressBar. */
	UPROPERTY(BlueprintReadOnly, Category = "Enemy Health Bar", meta = (BindWidget))
	TObjectPtr<UProgressBar> EnemyHealthProgressBar;
};
