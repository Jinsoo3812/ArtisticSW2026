#include "ShipAI/Abilities/EnemyShipTorpedo.h"

#include "AbilitySystemComponent.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Ship.h"

AEnemyShipTorpedo::AEnemyShipTorpedo()
{
	DamageGEClass = UGASDamageInstantGameplayEffect::StaticClass();
}

void AEnemyShipTorpedo::InitializeTorpedo(
	AShip* InLaunchingShip,
	AShip* InDesignatedTarget,
	float InSnapshotDamage,
	float InSpeed,
	const FVector& InDesignatedImpactLocation,
	float InImpactTolerance,
	float InMaximumFlightSeconds)
{
	DesignatedTarget = InDesignatedTarget;
	InitializeProjectile(InLaunchingShip, FMath::Max(0.0f, InSnapshotDamage), InSpeed);
	SetDesignatedImpactLocation(InDesignatedImpactLocation, InImpactTolerance);
	SetLifeSpan(FMath::Max(0.1f, InMaximumFlightSeconds));
}

void AEnemyShipTorpedo::HandleShipHit(AShip* HitShip)
{
	AShip* SourceShip = GetLaunchingShip();
	if (!HasAuthority() || !SourceShip || !HitShip
		|| HitShip->IsEnemyShipForEffects()
		|| !HitShip->ActorHasTag(TEXT("Player"))
		|| HitShip->ActorHasTag(TEXT("Enemy")))
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = SourceShip->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = HitShip->GetAbilitySystemComponent();
	if (SourceASC && TargetASC && DamageGEClass && DamageAmount > 0.0f)
	{
		const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeDamageEffectSpec(
			SourceASC,
			DamageGEClass,
			DamageAmount,
			SourceShip,
			this);
		if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		}
	}

	MulticastTorpedoExploded(GetActorLocation());
	Destroy();
}

void AEnemyShipTorpedo::MulticastTorpedoExploded_Implementation(const FVector& ExplosionLocation)
{
	K2_OnTorpedoExploded(ExplosionLocation);
}
