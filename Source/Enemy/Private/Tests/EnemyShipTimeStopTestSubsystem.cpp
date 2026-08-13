#include "Tests/EnemyShipTimeStopTestSubsystem.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipTimeStopField.h"

bool UEnemyShipTimeStopTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Param(FCommandLine::Get(), TEXT("EnemyShipTimeStopTest"))
		&& World
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UEnemyShipTimeStopTestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	WorldBeginTime = InWorld.GetTimeSeconds();
	UE_LOG(LogTemp, Warning, TEXT("[TIME-STOP-TEST][BEGIN] Map=%s NetMode=%d"),
		*InWorld.GetMapName(), static_cast<int32>(InWorld.GetNetMode()));
}

void UEnemyShipTimeStopTestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || bFinished)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (World->GetNetMode() == NM_Client)
	{
		TickClientProbe(Now);
		return;
	}

	if (ProbeStartTime <= 0.0 && Now - WorldBeginTime >= 3.0)
	{
		TryStartServerProbe();
	}
	if (ProbeStartTime > 0.0)
	{
		TickServerProbe(Now);
	}
	else if (Now - WorldBeginTime >= 45.0)
	{
		Finish(false, TEXT("No Player Ship found in Test_Level"));
	}
}

TStatId UEnemyShipTimeStopTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyShipTimeStopTestSubsystem, STATGROUP_Tickables);
}

void UEnemyShipTimeStopTestSubsystem::TryStartServerProbe()
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		const UNetDriver* NetDriver = World->GetNetDriver();
		const bool bClientJoined = NetDriver
			&& NetDriver->ClientConnections.ContainsByPredicate(
				[](const TObjectPtr<UNetConnection>& Connection)
				{
					return Connection && Connection->PlayerController;
				});
		if (!bClientJoined)
		{
			return;
		}
	}
	AShip* Ship = FindPlayerShip();
	if (!World || !Ship || !Ship->BuoyancyRoot)
	{
		return;
	}

	UClass* FieldClass = StaticLoadClass(
		AEnemyShipTimeStopField::StaticClass(), nullptr,
		TEXT("/Game/New/Enemy_Ship/Blueprints/BP_ES_TimeStopField.BP_ES_TimeStopField_C"));
	if (!FieldClass)
	{
		FieldClass = AEnemyShipTimeStopField::StaticClass();
	}

	TargetShip = Ship;
	AnchorLocation = Ship->BuoyancyRoot->GetComponentLocation();
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ProbeField = World->SpawnActor<AEnemyShipTimeStopField>(
		FieldClass, AnchorLocation, FRotator::ZeroRotator, Params);
	ProbeStartTime = World->GetTimeSeconds();
	if (!ProbeField.IsValid())
	{
		Finish(false, TEXT("Field spawn failed"));
		return;
	}
	ProbeField->InitializeTimeStop(1000.0f, 8.0f);
	UE_LOG(LogTemp, Warning,
		TEXT("[TIME-STOP-TEST][SERVER_SPAWNED] Ship=%s Field=%s Anchor=%s"),
		*GetNameSafe(Ship), *GetNameSafe(ProbeField.Get()), *AnchorLocation.ToCompactString());
}

void UEnemyShipTimeStopTestSubsystem::TickServerProbe(double Now)
{
	AShip* Ship = TargetShip.Get();
	if (!Ship || !Ship->BuoyancyRoot)
	{
		Finish(false, TEXT("Player Ship disappeared"));
		return;
	}

	const double Age = Now - ProbeStartTime;
	if (AEnemyShipTimeStopField* Field = ProbeField.Get())
	{
		MaximumObservedDrift = FMath::Max(
			MaximumObservedDrift,
			FVector::Distance(AnchorLocation, Ship->BuoyancyRoot->GetComponentLocation()));
		const bool bHasConstraint = Field->FindComponentByClass<UPhysicsConstraintComponent>() != nullptr;
		const bool bHasTag = Ship->GetAbilitySystemComponent()
			&& Ship->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Debuff_TimeStopped);
		bObservedActiveLock |= Ship->IsPropulsionSuppressed() && bHasConstraint && bHasTag;
	}

	if (!bAppliedImpulse && Age >= 0.5)
	{
		Ship->BuoyancyRoot->AddImpulse(FVector(5000000.0f, 3000000.0f, 2000000.0f));
		bAppliedImpulse = true;
		UE_LOG(LogTemp, Warning, TEXT("[TIME-STOP-TEST][SERVER_IMPULSE] Applied=true"));
	}

	if (Age >= 12.0)
	{
		const bool bHasTag = Ship->GetAbilitySystemComponent()
			&& Ship->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Debuff_TimeStopped);
		const bool bReleased = !Ship->IsPropulsionSuppressed() && !bHasTag;
		const bool bPassed = bObservedActiveLock && bAppliedImpulse
			&& MaximumObservedDrift <= 10.0f && bReleased;
		Finish(bPassed, bReleased ? TEXT("Completed") : TEXT("Time Stop did not release"));
	}
}

void UEnemyShipTimeStopTestSubsystem::TickClientProbe(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AEnemyShipTimeStopField> It(World); It; ++It)
	{
		AEnemyShipTimeStopField* Field = *It;
		for (const FEnemyShipTimeStopTarget& Entry : Field->GetAffectedTargetsForDiagnostics())
		{
			AShip* Ship = Entry.Type == EEnemyShipTimeStopTargetType::PlayerShip
				? Cast<AShip>(Entry.Actor) : nullptr;
			if (!Ship || !Ship->BuoyancyRoot)
			{
				continue;
			}
			TargetShip = Ship;
			AnchorLocation = Entry.Anchor.GetLocation();
			const bool bHasConstraint = Field->FindComponentByClass<UPhysicsConstraintComponent>() != nullptr;
			const bool bHasTag = Ship->GetAbilitySystemComponent()
				&& Ship->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Debuff_TimeStopped);
			const bool bActiveNow = bHasConstraint && bHasTag;
			if (bActiveNow && !bObservedActiveLock)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[TIME-STOP-TEST][CLIENT_ACTIVE] Ship=%s Constraint=true Tag=true"),
					*GetNameSafe(Ship));
			}
			bObservedActiveLock |= bActiveNow;
			MaximumObservedDrift = FMath::Max(
				MaximumObservedDrift,
				FVector::Distance(AnchorLocation, Ship->BuoyancyRoot->GetComponentLocation()));
			if (ProbeStartTime <= 0.0)
			{
				ProbeStartTime = Now;
			}
		}
	}

	if (ProbeStartTime > 0.0 && Now - ProbeStartTime >= 8.75)
	{
		AShip* Ship = TargetShip.Get();
		const bool bHasTag = Ship && Ship->GetAbilitySystemComponent()
			&& Ship->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Debuff_TimeStopped);
		const bool bPassed = Ship && bObservedActiveLock && !bHasTag
			&& MaximumObservedDrift <= 25.0f;
		Finish(bPassed, bHasTag ? TEXT("Client tag did not release") : TEXT("Completed"));
	}
	else if (Now - WorldBeginTime >= 45.0)
	{
		Finish(false, TEXT("No replicated Time Stop field received"));
	}
}

void UEnemyShipTimeStopTestSubsystem::Finish(bool bPassed, const TCHAR* Reason)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	UE_LOG(LogTemp, Warning,
		TEXT("[TIME-STOP-TEST][RESULT] Result=%s NetMode=%d ActiveLock=%s Impulse=%s MaxDrift=%.2f Reason=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"),
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		bObservedActiveLock ? TEXT("true") : TEXT("false"),
		bAppliedImpulse ? TEXT("true") : TEXT("false"),
		MaximumObservedDrift,
		Reason);
	if (FParse::Param(FCommandLine::Get(), TEXT("EnemyShipTimeStopTestAutoQuit")))
	{
		FPlatformMisc::RequestExit(false);
	}
}

AShip* UEnemyShipTimeStopTestSubsystem::FindPlayerShip() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		AShip* Ship = *It;
		if (IsValid(Ship) && !Ship->IsEnemyShipForEffects()
			&& Ship->ActorHasTag(TEXT("Player")) && !Ship->ActorHasTag(TEXT("Enemy")))
		{
			return Ship;
		}
	}
	return nullptr;
}
