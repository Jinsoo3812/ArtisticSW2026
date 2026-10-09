// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/EnemyShip.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Cannon.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "Item/ItemData.h"
#include "Settings_Item.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"
#include "Components/BaseHealthComponent.h"
#include "BaseAttributeSet.h"
#include "ShipAttributeSet.h"
#include "AIController.h"
#include "AI/BaseAIController.h"
#include "BrainComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "BuoyancyComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionChannels.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/StaticMesh.h"
#include "UI/EnemyHealthBarComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "BaseEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "SWCabinWaterCullComponent.h"
#include "DeckAI/DeckSpawnAnchorValidator.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Misc/ScopeExit.h"
#include "StoryFacadeSubsystem.h"
#include "Network/SWFinalEncounterDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyShipChestSpawnPoint, Log, All);
static const FName FinalBossSquadId(TEXT("Final"));
static const TCHAR* FinalBossShipArchetypePath = TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_TimeStop.DA_ES_TimeStop");

void AEnemyShip::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	AShip::CaptureRoomDomains(OutParts, OutIssues);
	FSWRoomEnemyShipState State;
	State.bDeathHandled = bDeathHandled;
	State.bHasDropped = bHasDropped;
	State.bCrewDefeated = bCrewDefeated;
	State.bHasEverHadLivingCrew = bHasEverHadLivingCrew;
 State.bStoryGateOpen = bStoryGateOpen;
 if (bDevelopmentStoryGateOpened)
 {
  const UStoryFacadeSubsystem* Story=GetGameInstance() ? GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
  if (!Story || !Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted)) State.bStoryGateOpen=false;
 }
	for (ABaseEnemy* Crew : RegisteredCrewEnemies)
	{
		if (!IsValid(Crew)) continue;
		if (const USWRoomSnapshotComponent* Id = Crew->FindComponentByClass<USWRoomSnapshotComponent>(); Id && Id->StableId.IsValid())
			State.CrewIds.AddUnique(Id->StableId);
		else
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Enemy");
			Issue.FieldKey = FName(*(TEXT("Crew:") + Crew->GetPathName()));
			Issue.Reason = TEXT("Crew member has no stable ID");
		}
	}
	if (RegisteredBoss)
		if (const USWRoomSnapshotComponent* Id = RegisteredBoss->FindComponentByClass<USWRoomSnapshotComponent>())
			State.BossId = Id->StableId;
	if (DeckEnemySpawnerComponent) DeckEnemySpawnerComponent->CaptureRoomState(State.DeckSpawner, OutIssues);
	if (BossEncounterComponent) BossEncounterComponent->CaptureRoomState(State.BossEncounter, OutIssues);
	State.CrewIds.Sort();
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Enemy;
	Part.Version = 4;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Enemy");
		Issue.FieldKey = TEXT("EnemyShipState");
		Issue.Reason = TEXT("Enemy ship serialization failed");
	}
}

bool AEnemyShip::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	if (Part.Domain == ESWRoomDomain::Ship) return AShip::RestoreRoomDomain(Part, OutError);
	FSWRoomEnemyShipState State;
	if (Part.Domain != ESWRoomDomain::Enemy || (Part.Version != 2 && Part.Version != 3 && Part.Version != 4)
		|| !FSWRoomStructCodec::Read(Part.Bytes, State))
	{
		OutError = TEXT("Invalid enemy ship state");
		return false;
	}
	if (Part.Version == 4 && State.DeckSpawner.LifecycleVersion != 1)
	{ OutError = TEXT("Enemy ship snapshot lacks deck lifecycle version"); return false; }
	if (DeckEnemySpawnerComponent && !DeckEnemySpawnerComponent->RestoreRoomState(State.DeckSpawner, OutError)) return false;
	if (BossEncounterComponent && !BossEncounterComponent->RestoreRoomState(State.BossEncounter, OutError)) return false;
	bDeathHandled = State.bDeathHandled;
	bHasDropped = State.bHasDropped;
	bCrewDefeated = State.bCrewDefeated;
	bHasEverHadLivingCrew = State.bHasEverHadLivingCrew;
	if (IsFinalBossSquadShip())
	{
		if (Part.Version >= 3) bStoryGateOpen = State.bStoryGateOpen;
		else if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			{
				bStoryGateOpen = Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted)
					&& !Story->IsStoryNodeReached(EStoryNode::FinalBossDefeated);
			}
		}
	}
	RegisteredCrewEnemies.Reset();
	RegisteredBoss = nullptr;
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AEnemyShip::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!AShip::FinalizeRoomRestore(RegisteredActors, OutError)) return false;
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	for (const FGuid& Id : PendingRoomState.CrewIds)
	{
		AActor* const* Found = RegisteredActors.Find(Id);
		ABaseEnemy* Crew = Found ? Cast<ABaseEnemy>(*Found) : nullptr;
		if (!Crew)
		{
			OutError = FString::Printf(TEXT("Enemy ship crew missing: %s"), *Id.ToString());
			return false;
		}
		RegisteredCrewEnemies.Add(Crew);
		Crew->OnBaseEnemyDeathNotified.AddUniqueDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
	}
	if (AActor* const* Found = RegisteredActors.Find(PendingRoomState.BossId))
		RegisteredBoss = Cast<AShipBossEnemy>(*Found);
	if (DeckEnemySpawnerComponent && !DeckEnemySpawnerComponent->FinalizeRoomState(RegisteredActors, OutError)) return false;
	if (BossEncounterComponent && !BossEncounterComponent->FinalizeRoomState(RegisteredActors, OutError)) return false;
	if (IsStoryGateDormant() && DeckEnemySpawnerComponent) DeckEnemySpawnerComponent->CancelDeployment();
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
	ApplyStoryGateToSpawnedChests();
	return true;
}

bool AEnemyShip::CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
	float TimeToleranceSeconds, TArray<FString>& OutFields) const
{
	if (Expected.Domain == ESWRoomDomain::Ship) return AShip::CompareRoomDomain(Expected, Actual, TimeToleranceSeconds, OutFields);
	if (Expected.Domain == ESWRoomDomain::Enemy && Actual.Domain == Expected.Domain
		&& (Expected.Version == 2 || Expected.Version == 3) && Actual.Version == 4)
	{
		FSWRoomEnemyShipState Before, After;
		if (!FSWRoomStructCodec::Read(Expected.Bytes, Before) || !FSWRoomStructCodec::Read(Actual.Bytes, After))
		{ OutFields.Add(TEXT("Field=Payload Expected=Readable Actual=Invalid")); return false; }
		if (Expected.Version == 2) After.bStoryGateOpen = Before.bStoryGateOpen;
		auto& A = After.DeckSpawner; const auto& B = Before.DeckSpawner;
		A.LifecycleVersion = B.LifecycleVersion; A.RequestState = B.RequestState;
		A.EncounterGeneration = B.EncounterGeneration; A.RequestId = B.RequestId; A.PlanSignature = B.PlanSignature;
		A.SlotResults = B.SlotResults; A.SlotEnemyIds = B.SlotEnemyIds; A.InitialTargetId = B.InitialTargetId;
		A.ReactionDelayRemaining = B.ReactionDelayRemaining; A.PendingSightRemaining = B.PendingSightRemaining;
		A.ReadinessTimeoutRemaining = B.ReadinessTimeoutRemaining; A.TargetWaitRemaining = B.TargetWaitRemaining;
		return FSWRoomStructCodec::CompareSaveGameStruct(FSWRoomEnemyShipState::StaticStruct(), &Before, &After, TimeToleranceSeconds, OutFields);
	}
	return FSWRoomStructCodec::Compare<FSWRoomEnemyShipState>(Expected, Actual, TimeToleranceSeconds, OutFields);
}

namespace
{
	TAutoConsoleVariable<int32> CVarShowEnemyShipAIDebug(
		TEXT("p.ShowEnemyShipAIDebug"),
		0,
		TEXT("Draw Enemy Ship AI ranges, state, abilities, and cooldowns. 0=off, 1=on."),
		ECVF_Cheat);

	TAutoConsoleVariable<float> CVarEnemyShipAIDebugHeight(
		TEXT("p.EnemyShipAIDebugHeight"),
		200.0f,
		TEXT("Vertical offset in cm for p.ShowEnemyShipAIDebug range lines."),
		ECVF_Cheat);
}

AEnemyShip::AEnemyShip()
{
	BossEncounterComponent = CreateDefaultSubobject<UBossEncounterComponent>(TEXT("BossEncounterComponent"));
	DeckEnemySpawnerComponent = CreateDefaultSubobject<UDeckEnemySpawnerComponent>(TEXT("DeckEnemySpawnerComponent"));
	DeckWalkAreaComponent = CreateDefaultSubobject<UDeckWalkAreaComponent>(TEXT("DeckWalkAreaComponent"));
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
	NavigationComponent = CreateDefaultSubobject<UEnemyShipNavigationComponent>(TEXT("EnemyShipNavigationComponent"));
	PatternRuntimeComponent = CreateDefaultSubobject<UEnemyShipPatternRuntimeComponent>(TEXT("EnemyShipPatternRuntimeComponent"));
	CabinWaterCullComponent = CreateDefaultSubobject<USWCabinWaterCullComponent>(TEXT("CabinWaterCullComponent"));
	EnemyHealthBarComponent = CreateDefaultSubobject<UEnemyHealthBarComponent>(TEXT("EnemyHealthBarComponent"));
	EnemyHealthBarComponent->SetupAttachment(RootComponent);

	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));
	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetCollisionProfileName(TEXT("EnemyShip"));
	}
	bEnableRollStabilization = true;
	if (ShipDamageMesh)
	{
		ShipDamageMesh->SetCollisionProfileName(TEXT("EnemyShipDamage"));
	}
}

void AEnemyShip::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyChestSpawnPointSettings();
}

void AEnemyShip::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (IsFinalBossSquadShip()) ApplyStoryGatePresentation();
	ApplyChestSpawnPointSettings();
}

void AEnemyShip::ApplyChestSpawnPointSettings()
{
	ChestSpawnPointLootSettings.SpawnMode = ChestSpawnPointChestSettings.SpawnMode;
	FChestSpawnPointChestSettings DeckChestSettings = ChestSpawnPointChestSettings;
	DeckChestSettings.Environment = EChestEnvironment::ShipDeck;
	DeckChestSettings.OwningShip = this;
	if (DeckChestSettings.SpawnMode == EChestSpawnMode::Guarded)
	{
		DeckChestSettings.GuardCharacters.Reset();
		for (ABaseEnemy* Crew : RegisteredCrewEnemies)
		{
			if (IsValid(Crew)) DeckChestSettings.GuardCharacters.AddUnique(Crew);
		}
	}

	TInlineComponentArray<UChildActorComponent*> ChildActorComponents(this);
	for (UChildActorComponent* ChildActorComponent : ChildActorComponents)
	{
		if (!ChildActorComponent)
		{
			continue;
		}

		UClass* ChildActorClass = ChildActorComponent->GetChildActorClass();
		if (!ChildActorClass || !ChildActorClass->IsChildOf(AChestSpawnPoint::StaticClass()))
		{
			if (ChildActorComponent->GetName().StartsWith(TEXT("ChestSpawnPoint")))
			{
				UE_LOG(LogEnemyShipChestSpawnPoint, Error,
					TEXT("%s.%s must use AChestSpawnPoint (or a subclass), but its Child Actor Class is %s."),
					*GetNameSafe(this), *ChildActorComponent->GetName(), *GetNameSafe(ChildActorClass));
			}
			continue;
		}

		if (AChestSpawnPoint* ChestSpawnPoint = Cast<AChestSpawnPoint>(ChildActorComponent->GetChildActor()))
		{
			ChestSpawnPoint->ApplyAuthoringSettings(
				DeckChestSettings,
				ChestSpawnPointLootSettings);
		}
	}
	if (BossEncounterComponent && HasAuthority() && HasActorBegunPlay())
	{
		BossEncounterComponent->RefreshChestReservations();
	}
}

void AEnemyShip::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
			RoomRestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &AEnemyShip::HandleRoomRestoreCompleted);
	}
	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));
	if (BuoyancyRoot)
	{
		FBodyInstance& BodyInstance = BuoyancyRoot->BodyInstance;
		BodyInstance.bLockXRotation = false;
		BodyInstance.SetDOFLock(BodyInstance.DOFMode);
	}
	if (NavigationComponent)
	{
		NavigationComponent->OnNavigationStateChanged.AddUniqueDynamic(
			this, &AEnemyShip::HandleNavigationStateChanged);
		ApplyNavigationCollisionPolicy(NavigationComponent->GetCurrentState());
	}

	// HealthComponent를 Ship의 ASC에 바인딩 (BaseEnemy의 패턴과 동일)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		// Join the crew's GAS team before spawning them so existing friendly-fire filters apply.
		if (HasAuthority() && !ASC->HasMatchingGameplayTag(Team_Enemy))
		{
			ASC->AddLooseGameplayTag(Team_Enemy);
		}

		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &AEnemyShip::OnDeathStarted);
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
	}

	if (EnemyHealthBarComponent)
	{
		EnemyHealthBarComponent->ConfigurePresentation(HealthBarOffset, HealthBarDrawSize);
		EnemyHealthBarComponent->SetVisibilitySourceComponent(ShipVisualMesh);
	}

	if (HasAuthority())
	{
		if (EnemyShipArchetype)
		{
			EnemyShipArchetype->ApplyToShip(this);
		}
	}

	// 군집 서브시스템에 등록
	if (HasAuthority())
	{
		if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
		{
			Weakening->RegisterShip(this);
		}
		InitializeDeckWaypoints();
		if (DeckWalkAreaComponent)
		{
			DeckWalkAreaComponent->Rebuild();
			if (!DeckWalkAreaComponent->IsReady())
			{
				UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Required walk area is unavailable on %s"),
					*GetName());
			}
		}
		if (DeckWalkAreaComponent && DeckWalkAreaComponent->IsReady()) InitializeDeckEnemyPool();

		if (NavigationComponent)
		{
			NavigationComponent->ClearAllOverrides();
			NavigationComponent->SetNavigationEnabled(false);
		}
		if (UShipSwarmSubsystem* SwarmSubsystem = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->RegisterShip(this);
		}
		if (IsFinalBossSquadShip())
		{
			static TWeakObjectPtr<UWorld> LastCountedFinalWorld;
			if (LastCountedFinalWorld.Get() != GetWorld())
			{
				LastCountedFinalWorld = GetWorld();
				int32 FinalCount = 0;
				int32 OwnerCount = 0;
				for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
				{
					if (!It->IsFinalBossSquadShip()) continue;
					++FinalCount;
					if (It->EnemyShipArchetype
						&& It->EnemyShipArchetype->GetPathName() == FinalBossShipArchetypePath) ++OwnerCount;
				}
				FSWFinalEncounterDiagnostics::Write(TEXT("FinalSquad"), TEXT("LevelCensus"),
					FString::Printf(TEXT("Count=%d BossShipCount=%d"), FinalCount, OwnerCount));
			}
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalSquad"), TEXT("ShipFound"),
				FString::Printf(TEXT("Ship=%s Label=%s Archetype=%s SquadID=%s GateOpen=%d"),
					*GetPathName(), *GetActorNameOrLabel(), *GetPathNameSafe(EnemyShipArchetype.Get()),
					*SquadID.ToString(), bStoryGateOpen));
			TInlineComponentArray<UChildActorComponent*> ChildActorComponents(this);
			for (UChildActorComponent* Component : ChildActorComponents)
			{
				if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Component->GetChildActor()))
				{
					FSWFinalEncounterDiagnostics::Write(TEXT("FinalSquad"), TEXT("ChestOwnership"),
						FString::Printf(TEXT("Ship=%s ParentOwnerMatches=%d Point=%s ChildOwnerMatches=%d"),
							*GetPathName(), ChestSpawnPointChestSettings.OwningShip == this,
							*Point->GetPathName(), Point->GetOwningShip() == this));
					Point->OnChestSpawned.AddUniqueDynamic(this, &AEnemyShip::HandleStoryGatedChestSpawned);
					if (AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor())) HandleStoryGatedChestSpawned(Chest);
				}
			}
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
				{
					Story->OnStoryChanged.AddUniqueDynamic(this, &AEnemyShip::HandleStoryGateChanged);
					HandleStoryGateChanged();
				}
				else if (!bStorySubsystemMissingLogged)
				{
					bStorySubsystemMissingLogged = true;
					FSWFinalEncounterDiagnostics::Write(TEXT("FinalGate"), TEXT("StoryMissing"), GetPathName());
				}
			}
		}
	}
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
	ApplyStoryGateToSpawnedChests();
}

void AEnemyShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	PublishRuntimeState();
	if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
		Room->OnRestoreCompleted.Remove(RoomRestoreCompletedHandle);
	if (bStoryGateCannonTagAdded)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent()) ASC->RemoveLooseGameplayTag(State_Ship_CannonDisabled);
		bStoryGateCannonTagAdded = false;
	}
	if (IsFinalBossSquadShip())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
				Story->OnStoryChanged.RemoveDynamic(this, &AEnemyShip::HandleStoryGateChanged);
		}
		TInlineComponentArray<UChildActorComponent*> ChildActorComponents(this);
		for (UChildActorComponent* Component : ChildActorComponents)
		{
			if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Component->GetChildActor()))
				Point->OnChestSpawned.RemoveDynamic(this, &AEnemyShip::HandleStoryGatedChestSpawned);
		}
	}
	if (bCaptureCannonTagAdded)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(State_Ship_CannonDisabled);
		}
		bCaptureCannonTagAdded = false;
	}
	RegisteredBoss = nullptr;
	for (ABaseEnemy* CrewEnemy : RegisteredCrewEnemies)
	{
		if (IsValid(CrewEnemy))
		{
			CrewEnemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
		}
	}
	if (HasAuthority())
	{
		if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
		{
			Weakening->UnregisterShip(this);
		}
		DestroyDeckEnemyPool();

		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			for (const FGameplayAbilitySpecHandle Handle : GrantedEnemyShipAbilityHandles)
			{
				ASC->ClearAbility(Handle);
			}
		}
		GrantedEnemyShipAbilityHandles.Reset();

		if (UShipSwarmSubsystem* SwarmSubsystem = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->UnregisterShip(this);
		}
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &AEnemyShip::OnDeathStarted);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	Super::EndPlay(EndPlayReason);
}

void AEnemyShip::InitializeDeckWaypoints()
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->InitializeWaypoints();
	}
}

void AEnemyShip::InitializeDeckEnemyPool()
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->InitializePool();
	}
}

void AEnemyShip::DestroyDeckEnemyPool()
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->Shutdown();
	}
}

void AEnemyShip::NotifyPlayerShipSighted(AShip* SensedPlayerShip)
{
	if (!HasAuthority() || bDeathHandled || bCrewDefeated || IsStoryGateDormant()
		|| !IsValid(SensedPlayerShip) || SensedPlayerShip == this
		|| SensedPlayerShip->IsEnemyShipForEffects()
		|| !SensedPlayerShip->ActorHasTag(TEXT("Player"))
		|| SensedPlayerShip->ActorHasTag(TEXT("Enemy")))
	{
		return;
	}

	if (BossEncounterComponent)
	{
		BossEncounterComponent->NotifyPlayerShipSighted(SensedPlayerShip);
	}
	if (DeckEnemySpawnerComponent)
	{
		if (DeckEnemySpawnerComponent->RequestDeployment(SensedPlayerShip))
		{
			UE_LOG(LogTemp, Log, TEXT("EnemySpawn"));
		}
	}
}

void AEnemyShip::NotifyPlayerShipSightLost(AShip* PlayerShip)
{
	if (HasAuthority() && DeckEnemySpawnerComponent) DeckEnemySpawnerComponent->RequestReadinessEvaluation();
}

bool AEnemyShip::AreAllOwnedDeckEnemiesDefeated() const
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->AreAllDeployedEnemiesDefeated();
}

int32 AEnemyShip::GetAliveOwnedDeckEnemyCount() const
{
	return DeckEnemySpawnerComponent
		? DeckEnemySpawnerComponent->GetAliveDeployedEnemyCount()
		: 0;
}

void AEnemyShip::NotifyOwnedDeckEnemyDefeated(ADeckEnemy* Enemy)
{
	if (HasAuthority() && DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->NotifyEnemyDefeated(Enemy);
	}
}

void AEnemyShip::NotifyAllOwnedDeckEnemiesDefeated()
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("EnemyDied"));
	OnOwnedDeckEnemiesDefeated.Broadcast(this);
	EvaluateCrewControlState();
}

void AEnemyShip::InitializeDefaultAttributes()
{
	if (!HasAuthority() || !AttributeSet)
	{
		return;
	}

	// AShip exposes a DataTable/Row pair for player and generic ships. Enemy ships
	// deliberately ignore those inherited fields; the selected Archetype applies
	// its SpecRow immediately after Super::BeginPlay returns.
	AttributeSet->InitHealth(100.0f);
	AttributeSet->InitMaxHealth(100.0f);
	AttributeSet->InitMoveSpeed(1.0f);
	AttributeSet->InitForwardPropulsionMultiplier(1.0f);
	AttributeSet->InitTurnTorqueMultiplier(1.0f);
	AttributeSet->InitCannonDamage(20.0f);
	AttributeSet->InitCannonFireCooldown(2.0f);
	AttributeSet->InitCannonballSpeed(3000.0f);
}

void AEnemyShip::HandleNavigationStateChanged(
	ENavalCombatState PreviousState,
	ENavalCombatState NewState)
{
	ApplyNavigationCollisionPolicy(NewState);
	if (!HasAuthority() || bDeathHandled || bCrewDefeated || !DeckEnemySpawnerComponent)
	{
		return;
	}
	if (NewState == ENavalCombatState::Approach
		|| NewState == ENavalCombatState::Orbit)
	{
		// Navigation target selection is distance-based; it must not bypass Sight.
		DeckEnemySpawnerComponent->RequestReadinessEvaluation();
	}
}

bool AEnemyShip::ActivateDeckEnemyAtPoint(
	int32 SpawnPointId,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	if (bCrewDefeated || IsStoryGateDormant())
	{
		OutEnemy = nullptr;
		return false;
	}
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->ActivateEnemyAtPoint(
			SpawnPointId, InitialTarget, OutEnemy);
}

bool AEnemyShip::ActivateDeckEnemyAtReservation(
	FDeckPointReservation& Reservation,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	if (bCrewDefeated || IsStoryGateDormant() || !DeckEnemySpawnerComponent)
	{
		Reservation.Reset();
		OutEnemy = nullptr;
		return false;
	}
	return DeckEnemySpawnerComponent->ActivateEnemyAtReservation(
		Reservation, InitialTarget, OutEnemy);
}

void AEnemyShip::ValidateDeckWaypoints()
{
#if WITH_EDITOR
	LastDeckWaypointValidationSummary = FDeckSpawnAnchorValidator::Validate(*this).ToSummary();
#else
	LastDeckWaypointValidationSummary = TEXT("Deck spawn anchor validation is editor-only.");
#endif
}

UDeckWaypointComponent* AEnemyShip::GetDeckWaypoint(int32 WaypointId) const
{
	return DeckEnemySpawnerComponent
		? DeckEnemySpawnerComponent->GetWaypoint(WaypointId)
		: nullptr;
}

FVector AEnemyShip::GetDeckWaypointWorldLocation(int32 WaypointId) const
{
	return DeckEnemySpawnerComponent
		? DeckEnemySpawnerComponent->GetWaypointWorldLocation(WaypointId)
		: GetActorLocation();
}

bool AEnemyShip::ResolveDeckCharacterTransform(
	int32 WaypointId,
	float CapsuleHalfHeight,
	FTransform& OutTransform) const
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->ResolveDeckCharacterTransform(
			WaypointId, CapsuleHalfHeight, OutTransform);
}

bool AEnemyShip::IsDeckPointAvailable(int32 WaypointId, const AActor* Requester) const
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->IsPointAvailable(WaypointId, Requester);
}

bool AEnemyShip::TryReserveDeckPoint(
	int32 WaypointId,
	AActor* Requester,
	FDeckPointReservation& OutReservation)
{
	if (!DeckEnemySpawnerComponent)
	{
		OutReservation.Reset();
		return false;
	}
	return DeckEnemySpawnerComponent->TryReservePoint(
		WaypointId, Requester, OutReservation);
}

bool AEnemyShip::TryReserveDeckEnemySpawnPoint(
	const FDeckEnemySpawnRequest& Request,
	FDeckPointReservation& OutReservation)
{
	if (!DeckEnemySpawnerComponent)
	{
		OutReservation.Reset();
		return false;
	}
	return DeckEnemySpawnerComponent->TryReserveEnemySpawnPoint(
		Request, OutReservation);
}

bool AEnemyShip::CommitDeckPointReservation(
	const FDeckPointReservation& Reservation,
	AActor* Occupant)
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->CommitPointReservation(Reservation, Occupant);
}

void AEnemyShip::ReleaseDeckPointReservation(FDeckPointReservation& Reservation)
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ReleasePointReservation(Reservation);
	}
	else
	{
		Reservation.Reset();
	}
}

bool AEnemyShip::TryOccupyDeckPoint(int32 WaypointId, AActor* Occupant)
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->TryOccupyPoint(WaypointId, Occupant);
}

void AEnemyShip::ReleaseDeckPointOccupancy(int32 WaypointId, AActor* Occupant)
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ReleasePointOccupancy(WaypointId, Occupant);
	}
}

void AEnemyShip::ReleaseAllDeckPointsFor(AActor* Actor)
{
	if (DeckWalkAreaComponent) DeckWalkAreaComponent->ReleaseLocationClaim(Actor);
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ReleaseAllPointsFor(Actor);
	}
}

bool AEnemyShip::GrantEnemyShipAbilityClasses(
	const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses)
{
	if (!HasAuthority())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	for (const FGameplayAbilitySpecHandle Handle : GrantedEnemyShipAbilityHandles)
	{
		ASC->ClearAbility(Handle);
	}
	GrantedEnemyShipAbilityHandles.Reset();

	TSet<UClass*> SeenClasses;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		if (AbilityClass && !SeenClasses.Contains(AbilityClass.Get()))
		{
			SeenClasses.Add(AbilityClass.Get());
			GrantedEnemyShipAbilityHandles.Add(ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1)));
		}
	}
	return GrantedEnemyShipAbilityHandles.Num() == SeenClasses.Num();
}

bool AEnemyShip::ConfigureEnemyShipArchetype(UEnemyShipArchetypeData* Archetype)
{
	if (!HasAuthority() || !Archetype || !NavigationComponent || !PatternRuntimeComponent)
	{
		return false;
	}

	PatternRuntimeComponent->Configure(Archetype);
	FEnemyShipNavigationProfile EffectiveNavigationProfile = Archetype->NavigationProfile;
	EffectiveNavigationProfile.bOrbitClockwise = false;
	NavigationComponent->SetNavigationProfile(EffectiveNavigationProfile);

	TArray<TSubclassOf<UGameplayAbility>> AbilityClasses;
	for (const UEnemyShipSkillModuleData* Module : Archetype->SkillModules)
	{
		if (IsValid(Module) && Module->AbilityClass)
		{
			AbilityClasses.AddUnique(Module->AbilityClass);
		}
	}
	if (!GrantEnemyShipAbilityClasses(AbilityClasses))
	{
		return false;
	}
	EnemyShipArchetype = Archetype;
	if (UWorld* World = GetWorld())
	{
		if (UShipSwarmSubsystem* SwarmSubsystem = World->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->RecalculateSquadOrbitDistances(SquadID);
		}
	}
	return true;
}

void AEnemyShip::SetSquadAssignedIdealDistance(float IdealDistance)
{
	if (!HasAuthority() || !NavigationComponent)
	{
		return;
	}
	FEnemyShipNavigationProfile Profile = EnemyShipArchetype
		? EnemyShipArchetype->NavigationProfile
		: NavigationComponent->GetNavigationProfile();
	Profile.bOrbitClockwise = false;
	Profile.IdealDistance = FMath::Max(1.0f, IdealDistance);
	NavigationComponent->SetNavigationProfile(Profile);
}

void AEnemyShip::ResetAfterReturnToSpawn()
{
	if (!HasAuthority() || bDeathHandled || IsSinking())
	{
		return;
	}

	SetAIControlInput(0.0f, 0.0f);
	FTransform SpawnTransform;
	if (NavigationComponent && NavigationComponent->GetSpawnHomeTransform(SpawnTransform))
	{
		SetActorLocationAndRotation(
			SpawnTransform.GetLocation(),
			SpawnTransform.GetRotation(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
		BuoyancyRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	if (HealthComponent)
	{
		HealthComponent->ResetForReuse();
	}
	if (PatternRuntimeComponent)
	{
		PatternRuntimeComponent->ResetRuntimeState();
	}
	bCrewDefeated = false;
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ResetForNewEncounter();
	}
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			Cannon->ResetAIFiringState();
		}
	}
	if (bCaptureCannonTagAdded)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(State_Ship_CannonDisabled);
		}
		bCaptureCannonTagAdded = false;
	}
	bHasEverHadLivingCrew = HasLivingCrew();
	EvaluateCrewControlState();
	OnRep_CrewDefeated();
	ForceNetUpdate();
}

void AEnemyShip::ApplyNavigationCollisionPolicy(ENavalCombatState State)
{
	const bool bActiveCombat = State == ENavalCombatState::Approach
		|| State == ENavalCombatState::Orbit;
	const bool bBlockSkillObstacles = State != ENavalCombatState::Return;

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(this);
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (!IsValid(Component)
			|| (Component->GetCollisionObjectType() != ECC_ShipHull
				&& Component->GetCollisionProfileName() != TEXT("ShipHullPhysics")))
		{
			continue;
		}
		Component->SetCollisionResponseToChannel(
			ECC_ShipHull,
			bActiveCombat ? ECR_Block : ECR_Ignore);
		Component->SetCollisionResponseToChannel(
			ECC_EnemyShipObstacle,
			bBlockSkillObstacles ? ECR_Block : ECR_Ignore);
	}
}

bool AEnemyShip::CanEnterDistanceOptimizationDormancy() const
{
	if (!bEnableDistanceOptimization || bDistanceOptimizationDormant || IsStoryGateDormant()
		|| bDeathHandled || IsSinking() || bCrewDefeated || !NavigationComponent
		|| NavigationComponent->GetCurrentState() != ENavalCombatState::Idle
		|| NavigationComponent->GetTargetShip() != nullptr
		|| NavigationComponent->HasActiveOverride())
	{
		return false;
	}
	if (DeckEnemySpawnerComponent && !DeckEnemySpawnerComponent->CanSuspendForDistanceOptimization()) return false;

	FVector HomeLocation;
	if (!NavigationComponent->GetResolvedHomeLocation(HomeLocation))
	{
		return false;
	}

	const float ArrivalDistance = FMath::Max(
		0.0f,
		NavigationComponent->GetNavigationProfile().ReturnArrivalDistance);
	return FVector::DistSquared2D(GetActorLocation(), HomeLocation)
		<= FMath::Square(ArrivalDistance);
}

void AEnemyShip::SetDistanceOptimizationDormant(bool bDormant)
{
	if (!HasAuthority() || bDistanceOptimizationDormant == bDormant)
	{
		return;
	}
	if (bDormant && !CanEnterDistanceOptimizationDormancy())
	{
		return;
	}
	if (!bDormant && (bDeathHandled || IsSinking()))
	{
		return;
	}

	if (!bDormant)
	{
		FlushNetDormancy();
		SetNetDormancy(DORM_Awake);
	}
	// Distance suspension preserves encounter/health. Only explicit home-return
	// completion may reset the ship and its pool for a new encounter.

	bDistanceOptimizationDormant = bDormant;
	ApplyEffectiveDormancyState();
	ForceNetUpdate();
	if (bDormant)
	{
		SetNetDormancy(DORM_DormantAll);
	}
}

void AEnemyShip::OnRep_DistanceOptimizationDormant()
{
	ApplyEffectiveDormancyState();
}

bool AEnemyShip::IsFinalBossSquadShip() const
{
	return SquadID == FinalBossSquadId;
}

bool AEnemyShip::IsStoryGateDormant() const
{
	return IsFinalBossSquadShip() && !bStoryGateOpen;
}

void AEnemyShip::HandleStoryGateChanged()
{
	if (!HasAuthority() || !IsFinalBossSquadShip()) return;
	UStoryFacadeSubsystem* Story = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
	if (!Story)
	{
		if (!bStorySubsystemMissingLogged)
		{
			bStorySubsystemMissingLogged = true;
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalGate"), TEXT("StoryMissing"), GetPathName());
		}
		return;
	}
	const bool bAccepted = Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted);
	const bool bDefeated = Story->IsStoryNodeReached(EStoryNode::FinalBossDefeated);
 const USWRoomProgressSubsystem* Room=GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
 const bool bTest=Room && Room->IsDevelopmentFinalEncounterWorld(GetWorld());
 const bool bTargetOpen=(bAccepted || bTest) && !bDefeated;
 bDevelopmentStoryGateOpened=bTest && !bAccepted && !bDefeated;
 if (bStoryGateOpen==bTargetOpen) return;
	FSWFinalEncounterDiagnostics::Write(TEXT("FinalGate"), TEXT("StoryEvaluated"),
		FString::Printf(TEXT("Ship=%s Accepted=%d Defeated=%d Open=%d"), *GetPathName(),
			bAccepted, bDefeated, bStoryGateOpen));
 SetStoryGateOpen(bTargetOpen);
}

void AEnemyShip::SetStoryGateOpen(bool bOpen)
{
	if (!HasAuthority() || !IsFinalBossSquadShip()) return;
	const bool bChanged = bStoryGateOpen != bOpen;
	if (bChanged && bOpen)
	{
		FlushNetDormancy();
		SetNetDormancy(DORM_Awake);
	}
	bStoryGateOpen = bOpen;
	if (!bOpen && DeckEnemySpawnerComponent) DeckEnemySpawnerComponent->CancelDeployment();
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
	ApplyStoryGateToSpawnedChests();
	if (bChanged)
	{
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalGate"), bOpen ? TEXT("Opened") : TEXT("Closed"), GetPathName());
		if (bOpen)
		{
			bool bAllOpen = true;
			for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
				if (It->IsFinalBossSquadShip() && !It->bStoryGateOpen) bAllOpen = false;
			static TWeakObjectPtr<UWorld> LastRecalculatedWorld;
			if (bAllOpen && LastRecalculatedWorld.Get() != GetWorld())
			{
				LastRecalculatedWorld = GetWorld();
				if (UShipSwarmSubsystem* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
					Swarm->RecalculateSquadOrbitDistances(FinalBossSquadId);
			}
		}
		ForceNetUpdate();
	}
}

void AEnemyShip::OnRep_StoryGateOpen()
{
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
	ApplyStoryGateToSpawnedChests();
}

void AEnemyShip::ApplyStoryGatePresentation()
{
	if (!IsFinalBossSquadShip()) return;
	RefreshMountedCannons();
	SetActorHiddenInGame(IsStoryGateDormant());
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			if (IsStoryGateDormant() && HasActorBegunPlay()) Cannon->ResetAIFiringState();
			Cannon->SetActorHiddenInGame(IsStoryGateDormant());
		}
	}
	if (!HasActorBegunPlay()) return;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (IsStoryGateDormant() && !bStoryGateCannonTagAdded)
		{
			ASC->AddLooseGameplayTag(State_Ship_CannonDisabled);
			bStoryGateCannonTagAdded = true;
		}
		else if (!IsStoryGateDormant() && bStoryGateCannonTagAdded)
		{
			ASC->RemoveLooseGameplayTag(State_Ship_CannonDisabled);
			bStoryGateCannonTagAdded = false;
		}
	}
}

void AEnemyShip::RefreshStoryGateOwnedActors()
{
	if (!IsFinalBossSquadShip()) return;
	if (HasAuthority()) HandleStoryGateChanged();
	// Reapply presentation after snapshot restoration even if the gate value
	// already matched the campaign when its change event was broadcast.
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
	ApplyStoryGateToSpawnedChests();
	RefreshMountedCannons();
	for (ACannon* Cannon : MountedCannons)
	{
		if (!IsValid(Cannon)) continue;
		if (bEffectiveDormancyApplied)
		{
			const bool bKnown = DormancyCannonStates.ContainsByPredicate(
				[Cannon](const FCannonDormancyState& State) { return State.Cannon.Get() == Cannon; });
			if (!bKnown)
			{
				FCannonDormancyState& State = DormancyCannonStates.AddDefaulted_GetRef();
				State.Cannon = Cannon;
				State.bCollisionEnabled = Cannon->GetActorEnableCollision();
				State.bTickEnabled = Cannon->IsActorTickEnabled();
				Cannon->SetActorEnableCollision(false);
				Cannon->SetActorTickEnabled(false);
			}
		}
		Cannon->SetActorHiddenInGame(IsStoryGateDormant());
	}
}

void AEnemyShip::HandleStoryGatedChestSpawned(AStorageChest* Chest)
{
	if (!HasAuthority() || !IsFinalBossSquadShip() || !IsValid(Chest)) return;
	const AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Chest->GetOwner());
	if (Point && Point->GetSpawnMode() == EChestSpawnMode::Guarded && Chest->GetOwningShip() == this)
	{
		Chest->SetStoryGateDormant(IsStoryGateDormant());
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalSquad"), TEXT("RuntimeChestOwnership"),
			FString::Printf(TEXT("Ship=%s Chest=%s Point=%s ChestOwnerMatches=%d Dormant=%d"),
				*GetPathName(), *Chest->GetPathName(), *Point->GetPathName(),
				Chest->GetOwningShip() == this, IsStoryGateDormant()));
	}
}

void AEnemyShip::ApplyStoryGateToSpawnedChests()
{
	if (!IsFinalBossSquadShip() || !HasAuthority()) return;
	TInlineComponentArray<UChildActorComponent*> ChildActorComponents(this);
	for (UChildActorComponent* Component : ChildActorComponents)
	{
		if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Component->GetChildActor()))
		{
			if (AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor()))
				HandleStoryGatedChestSpawned(Chest);
		}
	}
}

void AEnemyShip::ApplyEffectiveDormancyState()
{
	if (bApplyingRuntimeState) return;
	bApplyingRuntimeState = true;
	ON_SCOPE_EXIT { bApplyingRuntimeState = false; PublishRuntimeState(); };
	const auto* Room = HasAuthority() ? GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>() : nullptr;
	const bool bRestoring = Room && Room->IsRestoringSnapshot();
	const bool bShouldDormant = !HasAuthority() && RuntimeState.Revision != 0
		? (RuntimeState.Phase == EEnemyShipRuntimePhase::Dormant || RuntimeState.Phase == EEnemyShipRuntimePhase::Restoring)
		: bDistanceOptimizationDormant || IsStoryGateDormant() || bRestoring;
	if (bShouldDormant == bEffectiveDormancyApplied) return;
	if (IsFinalBossSquadShip())
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalGate"), bShouldDormant ? TEXT("Dormant") : TEXT("Active"),
			FString::Printf(TEXT("Ship=%s StoryDormant=%d DistanceDormant=%d Authority=%d"),
				*GetPathName(), IsStoryGateDormant(), bDistanceOptimizationDormant, HasAuthority()));
	if (bShouldDormant)
	{
		bEffectiveDormancyApplied = true;
		bDormancyShipCollisionEnabled = GetActorEnableCollision();
		bDormancyShipTickEnabled = IsActorTickEnabled();
		bDormancyShipPhysicsEnabled = IsShipRuntimePhysicsEnabled();
		SetAIControlInput(0.0f, 0.0f);
		if (HasAuthority())
		{
			if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
			{
				ASC->CancelAllAbilities();
			}
			if (NavigationComponent)
			{
				NavigationComponent->ClearAllOverrides();
				NavigationComponent->SetTargetShip(nullptr);
				NavigationComponent->SetNavigationEnabled(false);
			}
			if (AAIController* AIController = Cast<AAIController>(GetController()))
			{
				AIController->StopMovement();
				if (UBrainComponent* Brain = AIController->GetBrainComponent())
				{
					Brain->StopLogic(TEXT("Enemy ship distance optimization dormancy"));
				}
				if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
				{
					Perception->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
					Perception->Deactivate();
				}
				AIController->SetActorTickEnabled(false);
			}
		}

		DistanceDormancySuspendedTickComponents.Reset();
		TInlineComponentArray<UActorComponent*> Components(this);
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component) && Component->IsComponentTickEnabled())
			{
				DistanceDormancySuspendedTickComponents.Add(Component);
				Component->SetComponentTickEnabled(false);
			}
		}

		DistanceDormancySuspendedCannons.Reset();
		DormancyCannonStates.Reset();
		for (ACannon* Cannon : MountedCannons)
		{
			if (!IsValid(Cannon))
			{
				continue;
			}
			Cannon->ResetAIFiringState();
			FCannonDormancyState& State = DormancyCannonStates.AddDefaulted_GetRef();
			State.Cannon = Cannon;
			State.bCollisionEnabled = Cannon->GetActorEnableCollision();
			State.bTickEnabled = Cannon->IsActorTickEnabled();
			if (Cannon->IsActorTickEnabled())
			{
				DistanceDormancySuspendedCannons.Add(Cannon);
			}
			Cannon->SetActorTickEnabled(false);
			Cannon->SetActorEnableCollision(false);
		}

		SetActorEnableCollision(false);
		SetShipRuntimePhysicsEnabled(false);
		SetActorTickEnabled(false);
		return;
	}

	bEffectiveDormancyApplied = false;
	SetActorTickEnabled(bDormancyShipTickEnabled);
	SetActorEnableCollision(bDormancyShipCollisionEnabled);
	SetShipRuntimePhysicsEnabled(bDormancyShipPhysicsEnabled);
	for (const TWeakObjectPtr<UActorComponent>& Component : DistanceDormancySuspendedTickComponents)
	{
		if (Component.IsValid())
		{
			Component->SetComponentTickEnabled(true);
		}
	}
	DistanceDormancySuspendedTickComponents.Reset();

	for (const FCannonDormancyState& State : DormancyCannonStates)
	{
		if (ACannon* Cannon = State.Cannon.Get())
		{
			Cannon->SetActorTickEnabled(State.bTickEnabled);
			Cannon->SetActorEnableCollision(State.bCollisionEnabled);
			Cannon->RefreshPlayerInteractionAvailability();
		}
	}
	DormancyCannonStates.Reset();
	DistanceDormancySuspendedCannons.Reset();

	if (HasAuthority() && !IsStoryGateDormant() && !bCrewDefeated && !bDeathHandled && !IsSinking())
	{
		if (NavigationComponent)
		{
			NavigationComponent->SetNavigationEnabled(true);
		}
		if (AAIController* AIController = Cast<AAIController>(GetController()))
		{
			AIController->SetActorTickEnabled(true);
			if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
			{
				Perception->Activate(true);
				Perception->SetSenseEnabled(UAISense_Sight::StaticClass(), true);
				Perception->RequestStimuliListenerUpdate();
			}
			if (UBrainComponent* Brain = AIController->GetBrainComponent())
			{
				Brain->RestartLogic();
			}
		}
	}
}

bool AEnemyShip::CanDeployDeckEnemies() const
{
	const auto* Room = GetWorld() ? GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>() : nullptr;
	return HasAuthority() && RuntimeState.Revision != 0 && RuntimeState.IsActive()
		&& !bEndingPlay && !bApplyingRuntimeState && !bEffectiveDormancyApplied
		&& !bDistanceOptimizationDormant && !IsStoryGateDormant() && !bDeathHandled && !IsSinking()
		&& !bCrewDefeated && !(Room && Room->IsRestoringSnapshot());
}

void AEnemyShip::PublishRuntimeState()
{
	if (!HasAuthority() || bApplyingRuntimeState) return;
	FEnemyShipRuntimeState Next;
	if (bDistanceOptimizationDormant) Next.BlockingReasons |= static_cast<uint8>(EEnemyShipRuntimeBlock::Distance);
	if (IsStoryGateDormant()) Next.BlockingReasons |= static_cast<uint8>(EEnemyShipRuntimeBlock::Story);
	if (const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>(); Room && Room->IsRestoringSnapshot())
		Next.BlockingReasons |= static_cast<uint8>(EEnemyShipRuntimeBlock::Restore);
	if (bEndingPlay || bDeathHandled || IsSinking()) Next.BlockingReasons |= static_cast<uint8>(EEnemyShipRuntimeBlock::Terminal);
	if (bCrewDefeated) Next.BlockingReasons |= static_cast<uint8>(EEnemyShipRuntimeBlock::CrewDefeated);
	Next.Phase = (Next.BlockingReasons & static_cast<uint8>(EEnemyShipRuntimeBlock::Terminal)) ? EEnemyShipRuntimePhase::Terminal
		: (Next.BlockingReasons & static_cast<uint8>(EEnemyShipRuntimeBlock::Restore)) ? EEnemyShipRuntimePhase::Restoring
		: bEffectiveDormancyApplied ? EEnemyShipRuntimePhase::Dormant : EEnemyShipRuntimePhase::Active;
	if (RuntimeState.Revision && RuntimeState.Phase == Next.Phase && RuntimeState.BlockingReasons == Next.BlockingReasons) return;
	const FEnemyShipRuntimeState Previous = RuntimeState;
	Next.Revision = RuntimeState.Revision + 1;
	RuntimeState = Next;
	FlushNetDormancy();
	ForceNetUpdate();
	OnRuntimeStateChanged.Broadcast(Previous, RuntimeState);
}

void AEnemyShip::OnRep_RuntimeState()
{
	ApplyEffectiveDormancyState();
	OnRuntimePresentationChanged.Broadcast(LastClientRuntimeState, RuntimeState);
	LastClientRuntimeState = RuntimeState;
}

void AEnemyShip::HandleRoomRestoreCompleted()
{
	ApplyEffectiveDormancyState();
	ApplyStoryGatePresentation();
}

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
 if (HasAuthority() && IsFinalBossSquadShip())
 {
  const USWRoomProgressSubsystem* Room=GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
  if (Room && Room->IsDevelopmentFinalEncounterWorld(GetWorld())) HandleStoryGateChanged();
  else if (bDevelopmentStoryGateOpened) { SetStoryGateOpen(false); bDevelopmentStoryGateOpened=false; }
 }
	EvaluateCrewControlState();
	if (HasAuthority() && bCrewDefeated)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
			ASC && !ASC->HasMatchingGameplayTag(State_Ship_CannonDisabled))
		{
			ASC->AddLooseGameplayTag(State_Ship_CannonDisabled);
			bCaptureCannonTagAdded = true;
		}
	}

	if (CVarShowEnemyShipAIDebug.GetValueOnGameThread() > 0)
	{
		DrawEnemyShipAIDebug();
	}

}

void AEnemyShip::DrawEnemyShipAIDebug() const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !NavigationComponent)
	{
		return;
	}

	const FEnemyShipNavigationProfile& Profile = NavigationComponent->GetNavigationProfile();
	const float HeightOffset = FMath::Max(0.0f, CVarEnemyShipAIDebugHeight.GetValueOnGameThread());
	const FVector Center = GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
	constexpr int32 Segments = 96;
	constexpr float Thickness = 2.5f;
	constexpr uint8 DepthPriority = SDPG_Foreground;
	const FVector PlaneAxisX = FVector::ForwardVector;
	const FVector PlaneAxisY = FVector::RightVector;

	auto DrawRange = [World, PlaneAxisX, PlaneAxisY](
		const FVector& RangeCenter,
		float Radius,
		const FColor& Color,
		const TCHAR* Label)
	{
		if (Radius <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		DrawDebugCircle(
			World, RangeCenter, Radius, Segments, Color, false, 0.0f,
			DepthPriority, Thickness, PlaneAxisX, PlaneAxisY, false);
		DrawDebugString(
			World,
			RangeCenter + FVector(Radius, 0.0f, 15.0f),
			FString::Printf(TEXT("%s %.0fcm"), Label, Radius),
			nullptr,
			Color,
			0.0f,
			false,
			0.8f);
	};

	DrawRange(Center, Profile.IdealDistance, FColor::Green, TEXT("Ideal"));
	DrawRange(
		Center,
		Profile.IdealDistance + Profile.OrbitTolerance,
		FColor::Yellow,
		TEXT("OrbitMax"));
	DrawRange(Center, Profile.DetectionDistance, FColor::Cyan, TEXT("Detection"));

	FVector HomeLocation;
	const bool bHasHome = NavigationComponent->GetResolvedHomeLocation(HomeLocation);
	if (bHasHome)
	{
		const FVector HomeCenter = HomeLocation + FVector(0.0f, 0.0f, HeightOffset);
		DrawRange(HomeCenter, Profile.ReturnArrivalDistance, FColor::Magenta, TEXT("ReturnArrival"));
		DrawRange(HomeCenter, Profile.ReturnTriggerDistance, FColor::Orange, TEXT("ReturnTrigger"));
		DrawDebugLine(World, Center, HomeCenter, FColor::Magenta, false, 0.0f, DepthPriority, 1.5f);
	}

	const AShip* TargetShip = NavigationComponent->GetTargetShip();
	const float TargetDistance = TargetShip
		? FVector::Dist2D(GetActorLocation(), TargetShip->GetActorLocation())
		: -1.0f;
	if (TargetShip)
	{
		const FVector TargetPoint = TargetShip->GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
		DrawDebugLine(World, Center, TargetPoint, FColor::White, false, 0.0f, DepthPriority, 2.0f);
	}

	const FString StateName = StaticEnum<ENavalCombatState>()->GetNameStringByValue(
		static_cast<int64>(NavigationComponent->GetCurrentState()));
	const bool bReturning = NavigationComponent->GetCurrentState() == ENavalCombatState::Return;
	const float HomeDistance = bHasHome ? FVector::Dist2D(GetActorLocation(), HomeLocation) : -1.0f;
	const TCHAR* HomeSource = bHasHome ? TEXT("Spawn") : TEXT("None");
	FString CastingSummary = TEXT("None");
	FString AbilityDebugText;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		TArray<FString> ActiveAbilityNames;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability)
			{
				continue;
			}
			float Remaining = 0.0f;
			float Duration = 0.0f;
			Spec.Ability->GetCooldownTimeRemainingAndDuration(
				Spec.Handle, ASC->AbilityActorInfo.Get(), Remaining, Duration);
			const FString AbilityName = Spec.Ability->GetAssetTags().IsEmpty()
				? Spec.Ability->GetName()
				: Spec.Ability->GetAssetTags().ToStringSimple();
			const FString AbilityState = Spec.IsActive()
				? TEXT("ACTIVE")
				: Remaining > 0.0f
					? FString::Printf(TEXT("CD %.1f/%.1fs"), Remaining, Duration)
					: TEXT("READY");
			AbilityDebugText += FString::Printf(TEXT("\n- %s: %s"), *AbilityName, *AbilityState);

			if (Spec.IsActive())
			{
				FString ActiveName = AbilityName;
				if (Spec.Ability->GetAssetTags().HasTagExact(GameplayAbility_EnemyShip_Charge))
				{
					ActiveName += ASC->HasMatchingGameplayTag(State_EnemyShip_Charging)
						? TEXT(" [CHARGING]")
						: TEXT(" [AIMING]");
				}
				ActiveAbilityNames.Add(MoveTemp(ActiveName));
			}
		}
		if (!ActiveAbilityNames.IsEmpty())
		{
			CastingSummary = FString::Join(ActiveAbilityNames, TEXT(", "));
		}
	}

	FString DebugText = FString::Printf(
		TEXT("%s [%s]\nCASTING: %s\nNav=%s State=%s Override=%s\nTarget=%s Dist=%s\nReturn=%s Home=%s HomeDist=%s Trigger=%.0f Arrival=%.0f Propulsion=x%.2f\nArchetype=%s Skills=%d"),
		*GetName(),
		HasAuthority() ? TEXT("AUTH") : TEXT("CLIENT"),
		*CastingSummary,
		NavigationComponent->IsNavigationEnabled() ? TEXT("ON") : TEXT("OFF"),
		*StateName,
		NavigationComponent->HasActiveOverride() ? TEXT("YES") : TEXT("NO"),
		TargetShip ? *TargetShip->GetName() : TEXT("None"),
		TargetDistance >= 0.0f ? *FString::Printf(TEXT("%.0fcm"), TargetDistance) : TEXT("-"),
		bReturning ? TEXT("YES") : TEXT("NO"),
		HomeSource,
		HomeDistance >= 0.0f ? *FString::Printf(TEXT("%.0fcm"), HomeDistance) : TEXT("-"),
		Profile.ReturnTriggerDistance,
		Profile.ReturnArrivalDistance,
		Profile.ReturnPropulsionMultiplier,
		EnemyShipArchetype ? *EnemyShipArchetype->GetName() : TEXT("None"),
		PatternRuntimeComponent ? PatternRuntimeComponent->GetResolvedRuleCount() : 0);

	if (!AbilityDebugText.IsEmpty())
	{
		DebugText += TEXT("\nAbilities:") + AbilityDebugText;
	}

	int32 ReadyCannons = 0;
	FString CannonReloads;
	for (int32 Index = 0; Index < MountedCannons.Num(); ++Index)
	{
		const ACannon* Cannon = MountedCannons[Index];
		if (!IsValid(Cannon))
		{
			continue;
		}
		const bool bReady = Cannon->CanFireCannon();
		ReadyCannons += bReady ? 1 : 0;
		CannonReloads += FString::Printf(
			TEXT(" #%d:%s"),
			Index,
			bReady ? TEXT("READY") : *FString::Printf(TEXT("%.1fs"), Cannon->GetFireCooldownRemaining()));
	}
	DebugText += FString::Printf(
		TEXT("\nCannons=%d/%d READY%s"), ReadyCannons, MountedCannons.Num(), *CannonReloads);

	DrawDebugString(
		World,
		Center + FVector(0.0f, 0.0f, 350.0f),
		DebugText,
		nullptr,
		FColor::White,
		0.0f,
		true,
		1.0f);
}

void AEnemyShip::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (!bDeathHandled)
	{
		bDeathHandled = true;
		PublishRuntimeState();
		HandleShipDeath();
	}
}

void AEnemyShip::HandleShipDeath()
{
	if (!HasAuthority()) return;
	if (IsFinalBossSquadShip() && EnemyShipArchetype
		&& EnemyShipArchetype->GetPathName() == FinalBossShipArchetypePath)
	{
		UStoryFacadeSubsystem* Story = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
		const bool bAccepted = Story && Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted);
		const bool bAlreadyDefeated = Story && Story->IsStoryNodeReached(EStoryNode::FinalBossDefeated);
		bool bCompleted = false;
		if (bAccepted && !bAlreadyDefeated)
		{
			bCompleted = Story->CompleteStoryNode(EStoryNode::FinalBossDefeated);
		}
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalBossShip"), TEXT("Death"),
			FString::Printf(TEXT("Ship=%s Accepted=%d AlreadyDefeated=%d CompleteResult=%d"),
				*GetPathName(), bAccepted, bAlreadyDefeated, bCompleted));
	}
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->CancelDeployment();
	}
	SetAIControlInput(0.0f, 0.0f);
	if (NavigationComponent)
	{
		NavigationComponent->ClearAllOverrides();
		NavigationComponent->SetNavigationEnabled(false);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}

	const FVector DeathLocation = GetActorLocation();
	const FRotator DeathRotation = GetActorRotation();

	// 1. 사망 로그 출력 (이름 + 마지막 체력)
	float FinalHealth = 0.0f;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		FinalHealth = ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	}
	UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::HandleShipDeath - [%s] destroyed! Final Health: %.1f"), *GetName(), FinalHealth);

	// 2. AI Behavior Tree 먼저 정지 (AddForce 경고 방지)
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* BrainComp = AIC->GetBrainComponent())
		{
			BrainComp->StopLogic(TEXT("Ship Destroyed"));
		}
	}

	// 4. 대포 발사/조준 타이머 정지
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			Cannon->SetAIAimRotation(0.0f, 0.0f);
		}
	}
	DropAtDeathLocation(DeathLocation, DeathRotation);

	// 5. Player ships and enemy ships share the exact buoyancy-off/destruction path.
	StartSinking(DestroyAfterDeathDelay);
}

void AEnemyShip::DropAtDeathLocation(const FVector& DeathLocation, const FRotator& DeathRotation)
{
	if (!HasAuthority() || bHasDropped)
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - Drop skipped. Ship=%s HasAuthority=%d bHasDropped=%d"),
			*GetName(),
			HasAuthority() ? 1 : 0,
			bHasDropped ? 1 : 0);
		return;
	}
	bHasDropped = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - World is null. Ship=%s"), *GetName());
		return;
	}

	const FVector SpawnLocation = DeathLocation + EnemyCorpseStorageSpawnOffset;
	const FRotator SpawnRotation(0.0f, DeathRotation.Yaw, 0.0f);
	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

	TSubclassOf<AStorageChest> ChestClass = ChestSpawnPointChestSettings.ChestClassOverride;
	if (!ChestClass) ChestClass = AStorageChest::StaticClass();
	AStorageChest* SpawnedStorage = World->SpawnActorDeferred<AStorageChest>(
		ChestClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

	if (SpawnedStorage)
	{
		const int32 DropSeed = FMath::RandRange(1, MAX_int32);
		SpawnedStorage->ClearLegacyChestDefinition();
		SpawnedStorage->SetPhysicsAndBuoyancyEnabled(true);
		SpawnedStorage->FinishSpawning(SpawnTransform);
		TArray<FProgressionComputedDrop> SunkDrops;
		bool bHasProgressionDrops = false;
		for (TActorIterator<AGlobalLootSpawnManager> It(World); It; ++It)
		{
			bHasProgressionDrops = It->GetSunkChestDrops(ChestSpawnPointChestSettings.ProgressionZone, SunkDrops);
			break;
		}
		const USettings_Item* ItemSettings = GetDefault<USettings_Item>();
		const UItemData* Items = ItemSettings ? ItemSettings->ItemAssetRegistry.LoadSynchronous() : nullptr;
		if (bHasProgressionDrops && Items)
		{
			SpawnedStorage->ReplaceProgressionLoot(SunkDrops, Items, DropSeed);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Sunk chest spawned empty: no finalized progression drops or item definitions. Ship=%s Zone=%d"),
				*GetName(), static_cast<int32>(ChestSpawnPointChestSettings.ProgressionZone));
		}
		SpawnedStorage->ForceNetUpdate();

		UE_LOG(LogTemp, Log,
			TEXT("AEnemyShip::DropAtDeathLocation - Spawned buoyant progression chest. Ship=%s Zone=%d Location=%s"),
			*GetName(), static_cast<int32>(ChestSpawnPointChestSettings.ProgressionZone), *SpawnedStorage->GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AEnemyShip::DropAtDeathLocation - Failed to spawn sunk progression chest. Ship=%s Location=%s"),
			*GetName(), *SpawnLocation.ToString());
	}
}

bool AEnemyShip::AllowsPlayerAnchorControl(AActor* Interactor) const
{
	return !IsStoryGateDormant() && !bDeathHandled && bCrewDefeated;
}

float AEnemyShip::GetIncomingDamageMultiplier() const
{
	return bCrewDefeated && FMath::IsFinite(CrewDefeatedDamageMultiplier)
		? FMath::Max(1.0f, CrewDefeatedDamageMultiplier)
		: 1.0f;
}

float AEnemyShip::GetCannonCooldownMultiplier() const
{
	if (!EnemyShipArchetype)
	{
		return 1.0f;
	}
	const UShipAttributeSet* ShipAttributes = GetShipAttributeSet();
	const float HealthRatio = ShipAttributes && ShipAttributes->GetMaxHealth() > KINDA_SMALL_NUMBER
		? FMath::Clamp(ShipAttributes->GetHealth() / ShipAttributes->GetMaxHealth(), 0.0f, 1.0f)
		: 0.0f;
	const float ZeroHealthMultiplier = FMath::Max(
		1.0f,
		EnemyShipArchetype->ZeroHealthCannonCooldownMultiplier);
	return FMath::Lerp(ZeroHealthMultiplier, 1.0f, HealthRatio);
}

int32 AEnemyShip::GetLivingCrewCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<ABaseEnemy>& Crew : RegisteredCrewEnemies)
	{
		if (IsValid(Crew))
		{
			if (const UBaseHealthComponent* Health = Crew->GetHealthComponent())
			{
				if (!Health->IsDead())
				{
					++Count;
				}
			}
		}
	}

	return Count;
}

bool AEnemyShip::HasLivingCrew() const
{
	return GetLivingCrewCount() > 0;
}

void AEnemyShip::RegisterCrewEnemy(ABaseEnemy* CrewEnemy)
{
	if (!HasAuthority() || bCrewDefeated || !IsValid(CrewEnemy)
		|| RegisteredCrewEnemies.Contains(CrewEnemy)) return;
	if (const ADeckEnemy* DeckEnemy = Cast<ADeckEnemy>(CrewEnemy))
	{
		if (DeckEnemy->GetDeckHostShip() != this)
		{
			UE_LOG(LogEnemyShipChestSpawnPoint, Error, TEXT("Crew %s belongs to another deck"), *GetNameSafe(CrewEnemy));
			return;
		}
	}
	else if (CrewEnemy->GetOwner() && CrewEnemy->GetOwner() != this)
	{
		UE_LOG(LogEnemyShipChestSpawnPoint, Error, TEXT("Crew %s has another owner"), *GetNameSafe(CrewEnemy));
		return;
	}
	else if (!CrewEnemy->GetOwner()) CrewEnemy->SetOwner(this);
	const UBaseHealthComponent* Health = CrewEnemy->GetHealthComponent();
	if (!Health || Health->IsDead()) return;
	RegisteredCrewEnemies.Add(CrewEnemy);
	bHasEverHadLivingCrew = true;
	if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
	{
		Weakening->RegisterMember(this, CrewEnemy);
	}
	CrewEnemy->OnBaseEnemyDeathNotified.AddUniqueDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
	RegisterDeckEnemyChestGuard(CrewEnemy);
	EvaluateCrewControlState();
}

void AEnemyShip::NotifyCrewEnemyReactivated(ABaseEnemy* CrewEnemy)
{
	if (!HasAuthority() || bCrewDefeated || !RegisteredCrewEnemies.Contains(CrewEnemy)) return;
	RegisterDeckEnemyChestGuard(CrewEnemy);
	EvaluateCrewControlState();
}

bool AEnemyShip::RegisterBossEnemy(AShipBossEnemy* BossEnemy)
{
	if (!HasAuthority() || !IsValid(BossEnemy) || BossEnemy->GetHostShip() != this
		|| (IsValid(RegisteredBoss) && RegisteredBoss != BossEnemy)) return false;
	RegisteredBoss = BossEnemy;
	if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
	{
		Weakening->RegisterMember(this, BossEnemy);
	}
	return true;
}

bool AEnemyShip::IsOwnedCannonSplashProtectedActor(const AActor* Candidate) const
{
	return Candidate && (Candidate == RegisteredBoss
		|| RegisteredCrewEnemies.ContainsByPredicate([Candidate](const TObjectPtr<ABaseEnemy>& Crew)
		{
			return Crew.Get() == Candidate;
		}));
}

bool AEnemyShip::IsProtectedFromOwnHullCannonSplash(const AActor* Candidate) const
{
	return IsOwnedCannonSplashProtectedActor(Candidate);
}

void AEnemyShip::RegisterDeckEnemyChestGuard(ABaseEnemy* CrewEnemy)
{
	if (!HasAuthority() || !IsValid(CrewEnemy)) return;
	TInlineComponentArray<UChildActorComponent*> ChildActorComponents(this);
	for (UChildActorComponent* Component : ChildActorComponents)
	{
		if (Component)
		{
			if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Component->GetChildActor()))
			{
				if (Point->GetSpawnMode() == EChestSpawnMode::Guarded) Point->RegisterGuardCharacter(CrewEnemy);
			}
		}
	}
}

void AEnemyShip::UnregisterCrewEnemy(ABaseEnemy* CrewEnemy)
{
	if (HasAuthority() && IsValid(CrewEnemy) && RegisteredCrewEnemies.Contains(CrewEnemy))
	{
		if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
		{
			Weakening->UnregisterMember(this, CrewEnemy);
		}
		if (const UBaseHealthComponent* Health = CrewEnemy->GetHealthComponent(); Health && !Health->IsDead())
		{
			TInlineComponentArray<UChildActorComponent*> Components(this);
			for (UChildActorComponent* Component : Components)
			{
				if (AChestSpawnPoint* Point = Component ? Cast<AChestSpawnPoint>(Component->GetChildActor()) : nullptr)
				{
					Point->UnregisterGuardCharacter(CrewEnemy);
				}
			}
		}
		CrewEnemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
		RegisteredCrewEnemies.Remove(CrewEnemy);
		if (!bEndingPlay) EvaluateCrewControlState();
	}
}

void AEnemyShip::EvaluateCrewControlState()
{
	if (!HasAuthority())
	{
		return;
	}
	if (HasLivingCrew())
	{
		return;
	}
	if (!bHasEverHadLivingCrew)
	{
		return;
	}
	if (bCrewDefeated)
	{
		return;
	}

	bCrewDefeated = true;
	PublishRuntimeState();
	DisableEnemyShipAIForCapture();
	OnCrewDefeated.Broadcast(this);
	OnRep_CrewDefeated();
	ForceNetUpdate();
}

void AEnemyShip::DisableEnemyShipAIForCapture()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (!bCaptureCannonTagAdded)
		{
			ASC->AddLooseGameplayTag(State_Ship_CannonDisabled);
			bCaptureCannonTagAdded = true;
		}
	}
	SetAIControlInput(0.0f, 0.0f);
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->CancelDeployment();
	}

	if (NavigationComponent)
	{
		NavigationComponent->ClearAllOverrides();
		NavigationComponent->SetTargetShip(nullptr);
		NavigationComponent->SetNavigationEnabled(false);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* BrainComp = AIC->GetBrainComponent())
		{
			BrainComp->StopLogic(TEXT("Enemy ship crew defeated"));
		}
	}
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			Cannon->ResetAIFiringState();
		}
	}
}

void AEnemyShip::OnRep_CrewDefeated()
{
	PublishRuntimeState();
	UpdateHelmInteractionAvailability();
}

void AEnemyShip::HandleCrewEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason)
{
	if (Reason == EWaveEnemyRemoveReason::Death) EvaluateCrewControlState();
}

void AEnemyShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShip, bCrewDefeated);
	DOREPLIFETIME(AEnemyShip, bDistanceOptimizationDormant);
	DOREPLIFETIME(AEnemyShip, RuntimeState);
	DOREPLIFETIME(AEnemyShip, bStoryGateOpen);
}
