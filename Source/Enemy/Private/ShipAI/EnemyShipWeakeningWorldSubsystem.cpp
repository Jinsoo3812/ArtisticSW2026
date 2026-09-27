#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"

#include "ShipAI/EnemyShipWeakeningData.h"
#include "ShipAI/EnemyShipWeakeningSettings.h"
#include "ShipAI/EnemyShip.h"
#include "GAS/EnemyShipWeakeningEffect.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "Components/BaseHealthComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"

bool UEnemyShipWeakeningWorldSubsystem::IsServerWorld() const
{
	return GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UEnemyShipWeakeningWorldSubsystem::RegisterShip(AEnemyShip* Ship)
{
	if (!IsServerWorld() || !IsValid(Ship) || ShipHealth.Contains(Ship)) return;
	if (!bLoadedData)
	{
		bLoadedData = true;
		const UEnemyShipWeakeningSettings* Settings = GetDefault<UEnemyShipWeakeningSettings>();
		Data = Settings ? Settings->WeakeningData.LoadSynchronous() : nullptr;
		if (!Data) UE_LOG(LogTemp, Warning, TEXT("Enemy ship weakening data is not configured; using identity multipliers"));
	}
	UBaseHealthComponent* Health = Ship->FindComponentByClass<UBaseHealthComponent>();
	ShipHealth.Add(Ship, Health);
	if (Health)
	{
		Health->OnHealthChanged.AddUniqueDynamic(this, &UEnemyShipWeakeningWorldSubsystem::HandleShipHealthChanged);
	}
}

void UEnemyShipWeakeningWorldSubsystem::UnregisterShip(AEnemyShip* Ship)
{
	if (!IsServerWorld() || !Ship) return;
	TArray<ABaseEnemy*> MembersToRemove;
	for (const auto& Pair : MemberOwners)
	{
		if (Pair.Value.Get() == Ship && Pair.Key.IsValid()) MembersToRemove.Add(Pair.Key.Get());
	}
	for (ABaseEnemy* Member : MembersToRemove) UnregisterMember(Ship, Member);
	if (TWeakObjectPtr<UBaseHealthComponent>* Health = ShipHealth.Find(Ship))
	{
		if (Health->IsValid()) Health->Get()->OnHealthChanged.RemoveDynamic(
			this, &UEnemyShipWeakeningWorldSubsystem::HandleShipHealthChanged);
	}
	ShipHealth.Remove(Ship);
}

void UEnemyShipWeakeningWorldSubsystem::RegisterMember(AEnemyShip* Ship, ABaseEnemy* Member)
{
	if (!IsServerWorld() || !IsValid(Ship) || !IsValid(Member)) return;
	if (!ShipHealth.Contains(Ship)) RegisterShip(Ship);
	if (MemberOwners.Contains(Member)) return;
	MemberOwners.Add(Member, Ship);
	if (UBaseHealthComponent* Health = Member->GetHealthComponent())
	{
		Health->OnDeathStarted.AddUniqueDynamic(this, &UEnemyShipWeakeningWorldSubsystem::HandleMemberDeath);
	}
	RefreshMember(Member);
}

void UEnemyShipWeakeningWorldSubsystem::UnregisterMember(AEnemyShip* Ship, ABaseEnemy* Member)
{
	if (!IsServerWorld() || !Member || MemberOwners.FindRef(Member).Get() != Ship) return;
	RemoveMemberEffect(Member);
	if (UBaseHealthComponent* Health = Member->GetHealthComponent())
	{
		Health->OnDeathStarted.RemoveDynamic(this, &UEnemyShipWeakeningWorldSubsystem::HandleMemberDeath);
	}
	MemberOwners.Remove(Member);
}

void UEnemyShipWeakeningWorldSubsystem::BeforeMemberBaseStatsReset(ABaseEnemy* Member)
{
	if (IsServerWorld()) RemoveMemberEffect(Member);
}

void UEnemyShipWeakeningWorldSubsystem::AfterMemberBaseStatsReset(ABaseEnemy* Member)
{
	RefreshMember(Member);
}

void UEnemyShipWeakeningWorldSubsystem::RemoveMemberEffect(ABaseEnemy* Member)
{
	if (!IsValid(Member)) return;
	if (FActiveGameplayEffectHandle* Handle = MemberEffects.Find(Member))
	{
		if (UAbilitySystemComponent* ASC = Member->GetAbilitySystemComponent())
		{
			ASC->RemoveActiveGameplayEffect(*Handle);
		}
		MemberEffects.Remove(Member);
	}
}

void UEnemyShipWeakeningWorldSubsystem::RefreshMember(ABaseEnemy* Member)
{
	if (!IsServerWorld() || !IsValid(Member)) return;
	AEnemyShip* Ship = MemberOwners.FindRef(Member).Get();
	if (!IsValid(Ship)) return;
	RemoveMemberEffect(Member);
	UBaseHealthComponent* MemberHealth = Member->GetHealthComponent();
	UBaseHealthComponent* HullHealth = ShipHealth.FindRef(Ship).Get();
	if (!MemberHealth || MemberHealth->IsDead() || !HullHealth || HullHealth->IsDead()
		|| HullHealth->GetHealth() <= 0.f
		|| HullHealth->GetMaxHealth() <= 0.f || !Data) return;
	FEnemyShipWeakeningPoint Point;
	Data->Evaluate(HullHealth->GetHealth() / HullHealth->GetMaxHealth(), Point);
	if (FMath::IsNearlyEqual(Point.StrengthMultiplier, 1.f)
		&& FMath::IsNearlyEqual(Point.MoveSpeedMultiplier, 1.f)
		&& FMath::IsNearlyEqual(Point.AttackSpeedMultiplier, 1.f)) return;
	UAbilitySystemComponent* ASC = Member->GetAbilitySystemComponent();
	if (!ASC) return;
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UEnemyShipWeakeningEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid() || !Spec.Data.IsValid()) return;
	Spec.Data->SetSetByCallerMagnitude(Data_Effect_ShipCrewStrengthMultiplier, Point.StrengthMultiplier);
	Spec.Data->SetSetByCallerMagnitude(Data_Effect_ShipCrewMoveSpeedMultiplier, Point.MoveSpeedMultiplier);
	Spec.Data->SetSetByCallerMagnitude(Data_Effect_ShipCrewAttackSpeedMultiplier, Point.AttackSpeedMultiplier);
	MemberEffects.Add(Member, ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()));
}

void UEnemyShipWeakeningWorldSubsystem::RefreshShip(AEnemyShip* Ship)
{
	if (!IsServerWorld() || !Ship) return;
	for (const auto& Pair : MemberOwners)
	{
		if (Pair.Value.Get() == Ship && Pair.Key.IsValid()) RefreshMember(Pair.Key.Get());
	}
}

void UEnemyShipWeakeningWorldSubsystem::HandleShipHealthChanged(
	UBaseHealthComponent* Health, float OldValue, float NewValue, AActor* InstigatorActor)
{
	for (const auto& Pair : ShipHealth)
	{
		if (Pair.Value.Get() == Health && Pair.Key.IsValid()) RefreshShip(Pair.Key.Get());
	}
}

void UEnemyShipWeakeningWorldSubsystem::HandleMemberDeath(UBaseHealthComponent* Health)
{
	for (const auto& Pair : MemberOwners)
	{
		if (ABaseEnemy* Member = Pair.Key.Get(); Member && Member->GetHealthComponent() == Health)
		{
			RemoveMemberEffect(Member);
			break;
		}
	}
}

void UEnemyShipWeakeningWorldSubsystem::Deinitialize()
{
	for (const auto& Pair : MemberOwners)
	{
		if (ABaseEnemy* Member = Pair.Key.Get())
		{
			RemoveMemberEffect(Member);
			if (UBaseHealthComponent* Health = Member->GetHealthComponent())
			{
				Health->OnDeathStarted.RemoveDynamic(this, &UEnemyShipWeakeningWorldSubsystem::HandleMemberDeath);
			}
		}
	}
	MemberOwners.Empty();
	for (const auto& Pair : ShipHealth)
	{
		if (UBaseHealthComponent* Health = Pair.Value.Get())
		{
			Health->OnHealthChanged.RemoveDynamic(this, &UEnemyShipWeakeningWorldSubsystem::HandleShipHealthChanged);
		}
	}
	ShipHealth.Empty();
	Super::Deinitialize();
}
