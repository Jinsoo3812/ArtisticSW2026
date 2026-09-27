#include "UI/EnemyHealthBarWidget.h"

#include "Components/ProgressBar.h"

void UEnemyHealthBarWidget::SetHealthValues(float CurrentHealth, float MaxHealth)
{
	if (!EnemyHealthProgressBar)
	{
		return;
	}

	const float SafeMaxHealth = FMath::Max(0.0f, MaxHealth);
	const float SafeCurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, SafeMaxHealth);
	EnemyHealthProgressBar->SetPercent(
		SafeMaxHealth > KINDA_SMALL_NUMBER ? SafeCurrentHealth / SafeMaxHealth : 0.0f);
}
