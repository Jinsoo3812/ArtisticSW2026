#include "Upgrade/ShipUpgradeTypes.h"

bool FShipStatSnapshot::Equals(const FShipStatSnapshot& Other, float Tolerance) const
{
	return FMath::IsNearlyEqual(CannonDamage, Other.CannonDamage, Tolerance)
		&& FMath::IsNearlyEqual(CannonFireCooldownSeconds, Other.CannonFireCooldownSeconds, Tolerance)
		&& FMath::IsNearlyEqual(CannonballSpeed, Other.CannonballSpeed, Tolerance)
		&& FMath::IsNearlyEqual(MaxHealth, Other.MaxHealth, Tolerance)
		&& FMath::IsNearlyEqual(ForwardPropulsionMultiplier, Other.ForwardPropulsionMultiplier, Tolerance)
		&& FMath::IsNearlyEqual(TurnTorqueMultiplier, Other.TurnTorqueMultiplier, Tolerance);
}

FShipStatSnapshot FShipUpgradeCalculator::Calculate(
	const FShipStatSnapshot& BaseStats,
	const TArray<FShipUpgradeNodeDefinition>& Nodes,
	const TArray<FName>& ActiveNodeIds)
{
	FShipStatSnapshot Result = BaseStats;
	TMap<EShipStatType, float> FlatSums;
	TMap<EShipStatType, float> PercentSums;
	TSet<FName> UniqueActiveIds(ActiveNodeIds);

	TArray<const FShipUpgradeNodeDefinition*> ActiveNodes;
	for (const FShipUpgradeNodeDefinition& Node : Nodes)
	{
		if (UniqueActiveIds.Contains(Node.NodeId))
		{
			ActiveNodes.Add(&Node);
		}
	}
	ActiveNodes.Sort([](const FShipUpgradeNodeDefinition& A, const FShipUpgradeNodeDefinition& B)
	{
		return A.NodeId.LexicalLess(B.NodeId);
	});

	for (const FShipUpgradeNodeDefinition* Node : ActiveNodes)
	{
		for (const FShipStatModifier& Modifier : Node->StatModifiers)
		{
			TMap<EShipStatType, float>& Target = Modifier.Operation == EShipStatModifierOperation::AddFlat
				? FlatSums
				: PercentSums;
			Target.FindOrAdd(Modifier.StatType) += Modifier.Value;
		}
	}

	for (uint8 Index = 0; Index <= static_cast<uint8>(EShipStatType::TurnSpeed); ++Index)
	{
		const EShipStatType StatType = static_cast<EShipStatType>(Index);
		const float BaseValue = GetStatValue(BaseStats, StatType);
		const float FinalValue = (BaseValue + FlatSums.FindRef(StatType)) * (1.0f + PercentSums.FindRef(StatType));
		SetStatValue(Result, StatType, FinalValue);
	}

	Result.CannonDamage = FMath::Max(0.0f, Result.CannonDamage);
	Result.CannonFireCooldownSeconds = FMath::Max(0.05f, Result.CannonFireCooldownSeconds);
	Result.CannonballSpeed = FMath::Max(0.0f, Result.CannonballSpeed);
	Result.MaxHealth = FMath::Max(1.0f, Result.MaxHealth);
	Result.ForwardPropulsionMultiplier = FMath::Max(0.0f, Result.ForwardPropulsionMultiplier);
	Result.TurnTorqueMultiplier = FMath::Max(0.0f, Result.TurnTorqueMultiplier);
	return Result;
}

float FShipUpgradeCalculator::GetStatValue(const FShipStatSnapshot& Stats, EShipStatType StatType)
{
	switch (StatType)
	{
	case EShipStatType::CannonDamage: return Stats.CannonDamage;
	case EShipStatType::CannonFireCooldown: return Stats.CannonFireCooldownSeconds;
	case EShipStatType::CannonballSpeed: return Stats.CannonballSpeed;
	case EShipStatType::MaxHealth: return Stats.MaxHealth;
	case EShipStatType::ForwardPropulsion: return Stats.ForwardPropulsionMultiplier;
	case EShipStatType::TurnSpeed: return Stats.TurnTorqueMultiplier;
	default: return 0.0f;
	}
}

void FShipUpgradeCalculator::SetStatValue(FShipStatSnapshot& Stats, EShipStatType StatType, float Value)
{
	switch (StatType)
	{
	case EShipStatType::CannonDamage: Stats.CannonDamage = Value; break;
	case EShipStatType::CannonFireCooldown: Stats.CannonFireCooldownSeconds = Value; break;
	case EShipStatType::CannonballSpeed: Stats.CannonballSpeed = Value; break;
	case EShipStatType::MaxHealth: Stats.MaxHealth = Value; break;
	case EShipStatType::ForwardPropulsion: Stats.ForwardPropulsionMultiplier = Value; break;
	case EShipStatType::TurnSpeed: Stats.TurnTorqueMultiplier = Value; break;
	default: break;
	}
}

FText FShipUpgradeCalculator::GetStatDisplayName(EShipStatType StatType)
{
	switch (StatType)
	{
	case EShipStatType::CannonDamage: return NSLOCTEXT("ShipUpgrade", "CannonDamage", "대포 공격력");
	case EShipStatType::CannonFireCooldown: return NSLOCTEXT("ShipUpgrade", "CannonCooldown", "다음 발사까지의 시간");
	case EShipStatType::CannonballSpeed: return NSLOCTEXT("ShipUpgrade", "CannonballSpeed", "대포 발사속도");
	case EShipStatType::MaxHealth: return NSLOCTEXT("ShipUpgrade", "MaxHealth", "배 체력");
	case EShipStatType::ForwardPropulsion: return NSLOCTEXT("ShipUpgrade", "ForwardPropulsion", "배 WS 추진속도");
	case EShipStatType::TurnSpeed: return NSLOCTEXT("ShipUpgrade", "TurnSpeed", "배 AD 회전속도");
	default: return FText::GetEmpty();
	}
}

FText FShipUpgradeCalculator::GetStatUnit(EShipStatType StatType)
{
	switch (StatType)
	{
	case EShipStatType::CannonFireCooldown: return NSLOCTEXT("ShipUpgrade", "Seconds", "초");
	case EShipStatType::CannonballSpeed: return NSLOCTEXT("ShipUpgrade", "CentimetersPerSecond", "cm/s");
	case EShipStatType::ForwardPropulsion:
	case EShipStatType::TurnSpeed: return NSLOCTEXT("ShipUpgrade", "Multiplier", "배");
	default: return FText::GetEmpty();
	}
}

bool FShipUpgradeCalculator::IsPositiveDeltaBeneficial(EShipStatType StatType)
{
	return StatType != EShipStatType::CannonFireCooldown;
}
