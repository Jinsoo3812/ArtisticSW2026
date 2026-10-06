#include "BossAI/BossAttackPositionLibrary.h"

#include "BossAI/ShipBossEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

bool UBossAttackPositionLibrary::CanMeleeAttackFromCurrentPosition(const AShipBossEnemy* Boss, AActor* Target, float AttackRangeInset)
{
	if (!Boss || !Boss->CanEngageActor(Target) || Boss->IsBossHidden()) return false;
	const UBaseWeaponComponent* Weapon = Boss->GetWeaponComponent();
	const AEnemyShip* Ship = Boss->GetHostShip();
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation Self, Other;
	// This only narrows the decision to start a swing; the weapon's hit volume is unchanged.
	const float Range = Weapon ? FMath::Max(0.f, Weapon->GetCurrentAttackRange() - FMath::Max(0.f, AttackRangeInset)) : 0.f;
	if (!Weapon || !Weapon->IsWeaponEquipped() || !IsValid(Weapon->GetCurrentWeapon()) || Range <= 0.f
		|| !Area || !Area->ResolveActorOnDeck(*Boss, Self) || !Area->ResolveActorOnDeck(*Target, Other)
		|| Self.SurfaceId != Other.SurfaceId
		|| FVector::DistSquared(Boss->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(Range)) return false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BossVanishAttackLOS), true, Boss);
	Params.AddIgnoredActor(Weapon->GetCurrentWeapon());
	FHitResult Hit;
	return Boss->GetWorld() && (!Boss->GetWorld()->LineTraceSingleByChannel(
		Hit, Boss->GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params) || Hit.GetActor() == Target);
}
