#include "Upgrade/SharedShipUpgradeState.h"

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"
#include "MultiGameMode.h"
#include "Room/SWVoyageResetSubsystem.h"

ASharedShipUpgradeState::ASharedShipUpgradeState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	UpgradeComponent = CreateDefaultSubobject<UShipUpgradeComponent>(TEXT("SharedShipUpgradeComponent"));
	UpgradeComponent->bAutoLoadAndSaveLocalProgress = false;
}

void ASharedShipUpgradeState::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Warning,
		TEXT("[ShipUpgradePipeline][SharedStateBeginPlay] State=%s Authority=%s NetMode=%d Component=%s Tree=%s Nodes=%d CurrentShip=%s"),
		*GetNameSafe(this),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		static_cast<int32>(GetNetMode()),
		*GetNameSafe(UpgradeComponent),
		*GetNameSafe(UpgradeComponent ? UpgradeComponent->UpgradeTree.Get() : nullptr),
		UpgradeComponent && UpgradeComponent->UpgradeTree ? UpgradeComponent->UpgradeTree->Nodes.Num() : -1,
		*GetNameSafe(CurrentPlayerShip));
}

void ASharedShipUpgradeState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASharedShipUpgradeState, CurrentPlayerShip);
}

ASharedShipUpgradeState* ASharedShipUpgradeState::Find(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASharedShipUpgradeState> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

void ASharedShipUpgradeState::RegisterPlayerShip(AShip* Ship)
{
	if (!HasAuthority() || !IsValid(Ship) || Ship->IsEnemyShipForEffects() || !UpgradeComponent)
	{
		return;
	}

	CurrentPlayerShip = Ship;
	UpgradeComponent->OnShipStatsChanged.AddUniqueDynamic(
		this, &ASharedShipUpgradeState::HandleSharedStatsChanged);
	UpgradeComponent->SetPreviewBaseStats(Ship->GetBaseStatSnapshot());
	Ship->ApplyStatSnapshot(UpgradeComponent->GetCurrentShipStats(), true);
	ForceNetUpdate();

	UE_LOG(LogTemp, Log,
		TEXT("[SharedShipState] Registered Ship=%s ActiveNodes=%d"),
		*GetNameSafe(Ship), UpgradeComponent->GetActiveNodeIds().Num());
}

void ASharedShipUpgradeState::UnregisterPlayerShip(AShip* Ship)
{
	if (!HasAuthority() || CurrentPlayerShip != Ship)
	{
		return;
	}
	CurrentPlayerShip = nullptr;
	ForceNetUpdate();
}

void ASharedShipUpgradeState::HandleSharedStatsChanged(FShipStatSnapshot NewStats)
{
	if (HasAuthority() && IsValid(CurrentPlayerShip))
	{
		CurrentPlayerShip->ApplyStatSnapshot(NewStats, false);
		CurrentPlayerShip->ForceNetUpdate();
	}
}

FName ASharedShipUpgradeState::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<ASharedShipUpgradeState*>(this)) : NAME_None;
}

ESWVoyageStepResult ASharedShipUpgradeState::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!UpgradeComponent)
	{
		OutError = TEXT("SharedShipUpgradeComponentMissing");
		return ESWVoyageStepResult::Failed;
	}
	int32 Count = 0;
	for (TActorIterator<ASharedShipUpgradeState> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
	if (Count != 1)
	{
		OutError = TEXT("SharedShipUpgradeStateAmbiguous");
		return ESWVoyageStepResult::Failed;
	}
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASharedShipUpgradeState::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (UpgradeComponent) UpgradeComponent->OnShipStatsChanged.RemoveDynamic(this, &ASharedShipUpgradeState::HandleSharedStatsChanged);
	CurrentPlayerShip = nullptr;
	if (HasAuthority()) ForceNetUpdate();
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASharedShipUpgradeState::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!Context.bAuthority) return IsVoyageReady_Implementation(Context, OutError);
	AMultiGameMode* Mode = GetWorld()->GetAuthGameMode<AMultiGameMode>();
	AShip* Ship = Mode ? Cast<AShip>(Mode->GetPlayerRespawnShip()) : nullptr;
	if (!Ship) return ESWVoyageStepResult::Pending;
	const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || Voyage->GetActorGeneration(Ship) != Context.Generation || !UpgradeComponent)
	{
		OutError = TEXT("SharedShipUpgradeRebindContractInvalid");
		return ESWVoyageStepResult::Failed;
	}
	if (CurrentPlayerShip != Ship) RegisterPlayerShip(Ship);
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult ASharedShipUpgradeState::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!UpgradeComponent)
	{
		OutError = TEXT("SharedShipUpgradeComponentMissing");
		return ESWVoyageStepResult::Failed;
	}
	if (!IsValid(CurrentPlayerShip)) return ESWVoyageStepResult::Pending;
	const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{
		OutError = TEXT("SharedShipUpgradeGenerationMismatch");
		return ESWVoyageStepResult::Failed;
	}
	return CurrentPlayerShip->IsVoyagePhysicsReady(Context.Generation) ? ESWVoyageStepResult::Succeeded : ESWVoyageStepResult::Pending;
}

void ASharedShipUpgradeState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UpgradeComponent) UpgradeComponent->OnShipStatsChanged.RemoveDynamic(this, &ASharedShipUpgradeState::HandleSharedStatsChanged);
	CurrentPlayerShip = nullptr;
	Super::EndPlay(EndPlayReason);
}
