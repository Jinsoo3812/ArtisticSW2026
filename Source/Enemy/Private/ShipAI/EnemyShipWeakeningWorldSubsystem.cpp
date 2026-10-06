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
#include "EngineUtils.h"
#include "Room/SWVoyageResetSubsystem.h"

ESWVoyagePolicy UEnemyShipWeakeningWorldSubsystem::GetVoyagePolicy_Implementation() const { return ESWVoyagePolicy::ResetParticipant; }
FName UEnemyShipWeakeningWorldSubsystem::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	return Voyage ? Voyage->ResolveParticipantId(const_cast<UEnemyShipWeakeningWorldSubsystem*>(this)) : NAME_None;
}
ESWVoyageStepResult UEnemyShipWeakeningWorldSubsystem::PrepareVoyageReset_Implementation(const FSWVoyageResetContext&, FString& OutError)
{
	OutError.Reset(); bVoyageEventsDeferred = true;
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UEnemyShipWeakeningWorldSubsystem::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext&, FString& OutError)
{
	OutError.Reset(); ClearWorldBindings();
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UEnemyShipWeakeningWorldSubsystem::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{ OutError = TEXT("VoyageWeakeningGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	if (IsServerWorld())
		for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
		{
			if (!It->IsActorInitialized()) return ESWVoyageStepResult::Pending;
			if (Voyage->GetActorGeneration(*It) != Context.Generation)
			{ OutError = TEXT("VoyageWeakeningShipGenerationInvalid:") + It->GetPathName(); return ESWVoyageStepResult::Failed; }
			It->RebindVoyageWeakeningMembers();
		}
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UEnemyShipWeakeningWorldSubsystem::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{ OutError = TEXT("VoyageWeakeningGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	for (const auto& Pair : ShipHealth)
		if (!Pair.Key.IsValid() || Voyage->GetActorGeneration(Pair.Key.Get()) != Context.Generation)
		{ OutError = TEXT("VoyageWeakeningStaleShip"); return ESWVoyageStepResult::Failed; }
	for (const auto& Pair : MemberOwners)
		if (!Pair.Key.IsValid() || !Pair.Value.IsValid()
			|| Voyage->GetActorGeneration(Pair.Key.Get()) != Context.Generation || !ShipHealth.Contains(Pair.Value))
		{ OutError = TEXT("VoyageWeakeningStaleMember"); return ESWVoyageStepResult::Failed; }
	return ESWVoyageStepResult::Succeeded;
}
void UEnemyShipWeakeningWorldSubsystem::ResumeVoyage_Implementation(const FSWVoyageResetContext&)
{
	bVoyageEventsDeferred = false;
}
void UEnemyShipWeakeningWorldSubsystem::CancelVoyagePreparation_Implementation(const FSWVoyageResetContext&)
{
	bVoyageEventsDeferred = false;
}

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
	if (bVoyageEventsDeferred) return;
	for (const auto& Pair : ShipHealth)
	{
		if (Pair.Value.Get() == Health && Pair.Key.IsValid()) RefreshShip(Pair.Key.Get());
	}
}

void UEnemyShipWeakeningWorldSubsystem::HandleMemberDeath(UBaseHealthComponent* Health)
{
	if (bVoyageEventsDeferred) return;
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
	bVoyageEventsDeferred = true;
	ClearWorldBindings();
	Super::Deinitialize();
}

void UEnemyShipWeakeningWorldSubsystem::ClearWorldBindings()
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
	MemberEffects.Empty();
}
