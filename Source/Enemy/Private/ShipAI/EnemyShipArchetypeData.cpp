#include "ShipAI/EnemyShipArchetypeData.h"

#include "Misc/DataValidation.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipPatternData.h"

bool UEnemyShipArchetypeData::ApplyToShip(AEnemyShip* Ship) const
{
	if (!IsValid(Ship) || !Ship->HasAuthority() || !Pattern)
	{
		return false;
	}

	if (SpecRow.DataTable && !SpecRow.RowName.IsNone())
	{
		static const FString Context(TEXT("Enemy Ship Archetype"));
		const FShipStatRow* Row = SpecRow.GetRow<FShipStatRow>(Context);
		if (!Row)
		{
			return false;
		}

		FShipStatSnapshot Snapshot;
		Snapshot.MaxHealth = Row->MaxHealth;
		Snapshot.CannonDamage = Row->CannonDamage;
		Snapshot.CannonFireCooldownSeconds = Row->CannonFireCooldown;
		Snapshot.CannonballSpeed = Row->CannonballSpeed;
		Snapshot.ForwardPropulsionMultiplier = Row->ForwardPropulsionMultiplier;
		Snapshot.TurnTorqueMultiplier = Row->TurnTorqueMultiplier;
		if (FMath::IsNearlyEqual(Row->ForwardPropulsionMultiplier, 1.0f)
			&& FMath::IsNearlyEqual(Row->TurnTorqueMultiplier, 1.0f)
			&& !FMath::IsNearlyEqual(Row->ShipSpeedMultiplier, 1.0f))
		{
			Snapshot.ForwardPropulsionMultiplier = Row->ShipSpeedMultiplier;
			Snapshot.TurnTorqueMultiplier = Row->ShipSpeedMultiplier;
		}
		Ship->ApplyStatSnapshot(Snapshot, true);
	}

	return Ship->ConfigureEnemyShipPattern(Pattern);
}

EDataValidationResult UEnemyShipArchetypeData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Pattern)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Archetype requires a Pattern.")));
		Result = EDataValidationResult::Invalid;
	}
	if ((SpecRow.DataTable == nullptr) != SpecRow.RowName.IsNone())
	{
		Context.AddError(FText::FromString(TEXT("SpecRow must provide both DataTable and RowName, or neither.")));
		Result = EDataValidationResult::Invalid;
	}
	else if (SpecRow.DataTable && !SpecRow.GetRow<FShipStatRow>(TEXT("Enemy Ship Archetype Validation")))
	{
		Context.AddError(FText::FromString(TEXT("SpecRow does not resolve to FShipStatRow.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
