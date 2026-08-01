#include "RangedEnemy/RangedEnemyProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"

ARangedEnemyProjectile::ARangedEnemyProjectile()
{
	InitialLifeSpan = 10.0f;
}

bool ARangedEnemyProjectile::IsValidDamageTarget(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor) || TargetActor == GetOwner() || TargetActor == GetInstigator())
	{
		return false;
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(TargetActor));
	if (!TargetASC)
	{
		return false;
	}

	if (bOnlyDamagePlayers && !TargetASC->HasMatchingGameplayTag(Team_Player))
	{
		return false;
	}

	const UAbilitySystemComponent* ProjectileSourceASC = SourceASC;
	if (!ProjectileSourceASC)
	{
		AActor* SourceActor = GetInstigator() ? static_cast<AActor*>(GetInstigator()) : GetOwner();
		ProjectileSourceASC = SourceActor
			? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor)
			: nullptr;
	}

	if (ProjectileSourceASC)
	{
		const bool bBothPlayers = ProjectileSourceASC->HasMatchingGameplayTag(Team_Player)
			&& TargetASC->HasMatchingGameplayTag(Team_Player);
		const bool bBothEnemies = ProjectileSourceASC->HasMatchingGameplayTag(Team_Enemy)
			&& TargetASC->HasMatchingGameplayTag(Team_Enemy);
		if (bBothPlayers || bBothEnemies)
		{
			return false;
		}
	}

	return true;
}

bool ARangedEnemyProjectile::CanApplyDamageToActor(const AActor* OtherActor) const
{
	return IsValidDamageTarget(OtherActor);
}
