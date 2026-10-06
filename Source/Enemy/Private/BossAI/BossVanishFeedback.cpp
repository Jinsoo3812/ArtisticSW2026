#include "BossAI/BossVanishFeedback.h"

#include "AbilitySystemComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "ShipAI/EnemyShip.h"

void BossVanishFeedback::ExecuteAtLocation(AShipBossEnemy* Boss, FGameplayTag CueTag, const FVector& Location)
{
	// Server-only execution uses GAS replication. Do not execute a second local/client cue.
	if (!IsValid(Boss) || !Boss->HasAuthority() || !CueTag.IsValid() || Location.ContainsNaN()) return;
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	if (!ASC) return;
	FGameplayCueParameters Parameters;
	Parameters.Location = Location;
	Parameters.Normal = Boss->GetHostShip() && Boss->GetHostShip()->GetShipDeckMesh()
		? Boss->GetHostShip()->GetShipDeckMesh()->GetUpVector().GetSafeNormal() : FVector::UpVector;
	Parameters.Instigator = Boss;
	Parameters.EffectCauser = Boss;
	Parameters.bReplicateLocationWhenUsingMinimalRepProxy = true;
	ASC->ExecuteGameplayCue(CueTag, Parameters);
}
