// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/EnemyShip.h"
#include "Cannon.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"
#include "Components/BaseHealthComponent.h"
#include "BaseAttributeSet.h"
#include "AIController.h"
#include "AI/BaseAIController.h"
#include "BrainComponent.h"
#include "BuoyancyComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UI/EnemyHealthBarComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/Abilities/GA_EnemyShipDeployObstacle.h"
#include "ShipAI/Abilities/GA_EnemyShipTimeStop.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckNavigationComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "BaseEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#endif

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

#if WITH_EDITOR
	struct FGeneratedDeckSample
	{
		int32 GridX = INDEX_NONE;
		int32 GridY = INDEX_NONE;
		FVector LocalPosition = FVector::ZeroVector;
	};

	struct FGeneratedDeckSampleSet
	{
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		int32 GridCountX = 0;
		int32 GridCountY = 0;
		TArray<FGeneratedDeckSample> Samples;
	};

	int64 MakeDeckGridKey(int32 GridX, int32 GridY)
	{
		return (static_cast<int64>(GridX) << 32) | static_cast<uint32>(GridY);
	}

	bool TraceDeckSurface(
		UStaticMeshComponent& DeckMesh,
		const FBox& LocalBounds,
		float LocalX,
		float LocalY,
		const FDeckWaypointGenerationSettings& Settings,
		FVector& OutLocalPosition)
	{
		const FVector ComponentScale = DeckMesh.GetComponentScale().GetAbs();
		const float LocalTraceMargin = Settings.TraceMargin / FMath::Max(ComponentScale.Z, UE_SMALL_NUMBER);
		const FTransform DeckTransform = DeckMesh.GetComponentTransform();
		const FVector TraceStart = DeckTransform.TransformPosition(
			FVector(LocalX, LocalY, LocalBounds.Max.Z + LocalTraceMargin));
		const FVector TraceEnd = DeckTransform.TransformPosition(
			FVector(LocalX, LocalY, LocalBounds.Min.Z - LocalTraceMargin));

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenerateDeckWaypoint), true);
		QueryParams.bReturnPhysicalMaterial = false;
		FHitResult Hit;
		if (!DeckMesh.LineTraceComponent(Hit, TraceStart, TraceEnd, QueryParams))
		{
			return false;
		}

		const FVector DeckUp = DeckMesh.GetUpVector().GetSafeNormal();
		const float MinimumWalkableDot = FMath::Cos(FMath::DegreesToRadians(
			FMath::Clamp(Settings.MaximumWalkableSlope, 0.0f, 89.0f)));
		if (FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), DeckUp) < MinimumWalkableDot)
		{
			return false;
		}

		OutLocalPosition = DeckTransform.InverseTransformPosition(Hit.ImpactPoint);
		return true;
	}

	bool IsDeckSampleSupported(
		UStaticMeshComponent& DeckMesh,
		const FBox& LocalBounds,
		float LocalX,
		float LocalY,
		const FDeckWaypointGenerationSettings& Settings,
		FVector& OutLocalPosition)
	{
		if (!TraceDeckSurface(DeckMesh, LocalBounds, LocalX, LocalY, Settings, OutLocalPosition))
		{
			return false;
		}

		const float Clearance = FMath::Max(0.0f, Settings.EdgeClearance);
		if (Clearance <= UE_SMALL_NUMBER)
		{
			return true;
		}

		const FVector ComponentScale = DeckMesh.GetComponentScale().GetAbs();
		const FTransform DeckTransform = DeckMesh.GetComponentTransform();
		const FVector CenterWorld = DeckTransform.TransformPosition(OutLocalPosition);
		const FVector DeckUp = DeckMesh.GetUpVector().GetSafeNormal();
		constexpr int32 ClearanceSampleCount = 8;
		for (int32 SampleIndex = 0; SampleIndex < ClearanceSampleCount; ++SampleIndex)
		{
			const float Angle = 2.0f * UE_PI * static_cast<float>(SampleIndex)
				/ static_cast<float>(ClearanceSampleCount);
			const float OffsetX = FMath::Cos(Angle) * Clearance / FMath::Max(ComponentScale.X, UE_SMALL_NUMBER);
			const float OffsetY = FMath::Sin(Angle) * Clearance / FMath::Max(ComponentScale.Y, UE_SMALL_NUMBER);
			FVector SupportLocal;
			if (!TraceDeckSurface(
				DeckMesh, LocalBounds, LocalX + OffsetX, LocalY + OffsetY, Settings, SupportLocal))
			{
				return false;
			}

			const FVector SupportWorld = DeckTransform.TransformPosition(SupportLocal);
			const float HeightDelta = FMath::Abs(FVector::DotProduct(SupportWorld - CenterWorld, DeckUp));
			if (HeightDelta > FMath::Max(0.0f, Settings.MaximumStepHeight))
			{
				return false;
			}
		}
		return true;
	}

	FVector GetDeckLocalWaypointLocation(
		const UDeckWaypointComponent& Waypoint,
		const UStaticMeshComponent& DeckMesh)
	{
		if (Waypoint.IsRegistered() && DeckMesh.IsRegistered())
		{
			return DeckMesh.GetComponentTransform().InverseTransformPosition(
				Waypoint.GetComponentLocation());
		}
		return Waypoint.GetRelativeLocation();
	}

	bool IsDeckConnectionSupported(
		UStaticMeshComponent& DeckMesh,
		const FBox& LocalBounds,
		const FVector& Start,
		const FVector& End,
		const FDeckWaypointGenerationSettings& Settings)
	{
		FDeckWaypointGenerationSettings LinkSettings = Settings;
		LinkSettings.EdgeClearance = 0.0f;
		const FTransform DeckTransform = DeckMesh.GetComponentTransform();
		const float Distance = FVector::Dist2D(
			DeckTransform.TransformPosition(Start),
			DeckTransform.TransformPosition(End));
		const int32 SegmentCount = FMath::Max(
			1,
			FMath::CeilToInt(Distance / FMath::Max(10.0f, Settings.AutomaticLinkSampleSpacing)));
		FVector PreviousSupportWorld = DeckTransform.TransformPosition(Start);
		const FVector DeckUp = DeckMesh.GetUpVector().GetSafeNormal();
		for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const FVector Desired = FMath::Lerp(Start, End, Alpha);
			FVector Support;
			if (!IsDeckSampleSupported(
				DeckMesh, LocalBounds, Desired.X, Desired.Y, LinkSettings, Support))
			{
				return false;
			}
			const FVector SupportWorld = DeckTransform.TransformPosition(Support);
			if (SegmentIndex > 0
				&& FMath::Abs(FVector::DotProduct(SupportWorld - PreviousSupportWorld, DeckUp))
					> FMath::Max(0.0f, Settings.MaximumStepHeight))
			{
				return false;
			}
			PreviousSupportWorld = SupportWorld;
		}
		return true;
	}

	int32 AssignDeckWaypointIds(
		TArray<UDeckWaypointComponent*>& Waypoints,
		const FDeckWaypointGenerationSettings& Settings)
	{
		Waypoints.Sort([](const UDeckWaypointComponent& Left, const UDeckWaypointComponent& Right)
		{
			return Left.GetName() < Right.GetName();
		});

		const int32 GeneratedBase = FMath::Max(2, Settings.GeneratedWaypointIdBase);
		const int32 ManualBase = FMath::Clamp(Settings.ManualWaypointIdBase, 1, GeneratedBase - 1);
		int32 NextManualId = ManualBase;
		int32 NextGeneratedId = GeneratedBase;
		TSet<int32> UsedIds;
		int32 ChangedCount = 0;

		for (UDeckWaypointComponent* Waypoint : Waypoints)
		{
			if (!IsValid(Waypoint))
			{
				continue;
			}
			const int32 CurrentId = Waypoint->GetWaypointId();
			const bool bGenerated = Waypoint->WasGeneratedFromDeckMesh();
			const bool bInExpectedRange = bGenerated
				? CurrentId >= GeneratedBase
				: CurrentId >= ManualBase && CurrentId < GeneratedBase;
			if (bInExpectedRange && !UsedIds.Contains(CurrentId))
			{
				UsedIds.Add(CurrentId);
				continue;
			}

			int32& CandidateId = bGenerated ? NextGeneratedId : NextManualId;
			const int32 MaximumId = bGenerated ? MAX_int32 : GeneratedBase - 1;
			while (CandidateId < MaximumId && UsedIds.Contains(CandidateId))
			{
				++CandidateId;
			}
			if (UsedIds.Contains(CandidateId))
			{
				continue;
			}
			Waypoint->Modify();
			Waypoint->SetWaypointIdForAuthoring(CandidateId);
			UsedIds.Add(CandidateId);
			++CandidateId;
			++ChangedCount;
		}
		return ChangedCount;
	}

	int32 RebuildDeckWaypointLinks(
		TArray<UDeckWaypointComponent*>& Waypoints,
		UStaticMeshComponent& DeckMesh,
		const FDeckWaypointGenerationSettings& Settings)
	{
		const UStaticMesh* DeckAsset = DeckMesh.GetStaticMesh();
		if (!DeckAsset)
		{
			return 0;
		}

		TMap<int32, UDeckWaypointComponent*> WaypointById;
		TMap<UDeckWaypointComponent*, TArray<int32>> LinksByWaypoint;
		for (UDeckWaypointComponent* Waypoint : Waypoints)
		{
			if (!IsValid(Waypoint))
			{
				continue;
			}
			WaypointById.FindOrAdd(Waypoint->GetWaypointId(), Waypoint);
			TArray<int32>& Links = LinksByWaypoint.FindOrAdd(Waypoint);
			if (!Waypoint->AllowsAutomaticLinks())
			{
				for (const int32 LinkedId : Waypoint->GetLinkedWaypointIds())
				{
					if (LinkedId != Waypoint->GetWaypointId())
					{
						Links.AddUnique(LinkedId);
					}
				}
			}
		}

		for (const TPair<UDeckWaypointComponent*, TArray<int32>>& Pair : LinksByWaypoint)
		{
			if (Pair.Key->AllowsAutomaticLinks())
			{
				continue;
			}
			for (const int32 LinkedId : Pair.Value)
			{
				if (UDeckWaypointComponent* const* Linked = WaypointById.Find(LinkedId))
				{
					LinksByWaypoint.FindOrAdd(*Linked).AddUnique(Pair.Key->GetWaypointId());
				}
			}
		}

		const FBox LocalBounds = DeckAsset->GetBoundingBox();
		int32 LinkCount = 0;
		for (int32 FirstIndex = 0; FirstIndex < Waypoints.Num(); ++FirstIndex)
		{
			UDeckWaypointComponent* First = Waypoints[FirstIndex];
			if (!IsValid(First) || !First->AllowsAutomaticLinks())
			{
				continue;
			}
			const FVector FirstLocation = GetDeckLocalWaypointLocation(*First, DeckMesh);
			const float FirstRadius = First->GetAutomaticLinkDistanceOverride() > 0.0f
				? First->GetAutomaticLinkDistanceOverride()
				: Settings.AutomaticLinkDistance;
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Waypoints.Num(); ++SecondIndex)
			{
				UDeckWaypointComponent* Second = Waypoints[SecondIndex];
				if (!IsValid(Second) || !Second->AllowsAutomaticLinks())
				{
					continue;
				}
				const FVector SecondLocation = GetDeckLocalWaypointLocation(*Second, DeckMesh);
				const float SecondRadius = Second->GetAutomaticLinkDistanceOverride() > 0.0f
					? Second->GetAutomaticLinkDistanceOverride()
					: Settings.AutomaticLinkDistance;
				if (FVector::Dist2D(
						DeckMesh.GetComponentTransform().TransformPosition(FirstLocation),
						DeckMesh.GetComponentTransform().TransformPosition(SecondLocation))
						> FMath::Min(FirstRadius, SecondRadius)
					|| !IsDeckConnectionSupported(
						DeckMesh, LocalBounds, FirstLocation, SecondLocation, Settings))
				{
					continue;
				}
				LinksByWaypoint.FindOrAdd(First).AddUnique(Second->GetWaypointId());
				LinksByWaypoint.FindOrAdd(Second).AddUnique(First->GetWaypointId());
				++LinkCount;
			}
		}

		for (TPair<UDeckWaypointComponent*, TArray<int32>>& Pair : LinksByWaypoint)
		{
			Pair.Key->Modify();
			Pair.Key->SetLinkedWaypointIdsForAuthoring(Pair.Value);
		}
		return LinkCount;
	}

	bool IsPlacedEditorActor(const AActor& Actor)
	{
		const UWorld* World = Actor.GetWorld();
		return !Actor.HasAnyFlags(RF_ClassDefaultObject)
			&& World
			&& World->WorldType == EWorldType::Editor;
	}

	bool IsBlueprintAssetAuthoringContext(const AActor& Actor)
	{
		const UWorld* World = Actor.GetWorld();
		return Actor.HasAnyFlags(RF_ClassDefaultObject)
			|| (World && World->WorldType == EWorldType::EditorPreview);
	}

	UBlueprint* GetActorBlueprint(const AActor& Actor)
	{
		return Cast<UBlueprint>(Actor.GetClass()->ClassGeneratedBy);
	}

	bool BuildGeneratedDeckSamples(AEnemyShip& Ship, FGeneratedDeckSampleSet& OutSampleSet)
	{
		UStaticMeshComponent* DeckMesh = Ship.GetShipDeckMesh();
		if (!DeckMesh || !DeckMesh->GetStaticMesh() || !DeckMesh->IsRegistered())
		{
			return false;
		}

		OutSampleSet = FGeneratedDeckSampleSet();
		OutSampleSet.LocalBounds = DeckMesh->GetStaticMesh()->GetBoundingBox();
		const FVector ComponentScale = DeckMesh->GetComponentScale().GetAbs();
		const FDeckWaypointGenerationSettings& Settings = Ship.DeckWaypointGenerationSettings;
		const float Spacing = FMath::Max(25.0f, Settings.GridSpacing);
		const float LocalSpacingX = Spacing / FMath::Max(ComponentScale.X, UE_SMALL_NUMBER);
		const float LocalSpacingY = Spacing / FMath::Max(ComponentScale.Y, UE_SMALL_NUMBER);
		OutSampleSet.GridCountX = FMath::Max(
			1, FMath::CeilToInt(OutSampleSet.LocalBounds.GetSize().X / LocalSpacingX));
		OutSampleSet.GridCountY = FMath::Max(
			1, FMath::CeilToInt(OutSampleSet.LocalBounds.GetSize().Y / LocalSpacingY));

		for (int32 GridY = 0; GridY < OutSampleSet.GridCountY; ++GridY)
		{
			const float LocalY = FMath::Lerp(
				OutSampleSet.LocalBounds.Min.Y,
				OutSampleSet.LocalBounds.Max.Y,
				(static_cast<float>(GridY) + 0.5f) / static_cast<float>(OutSampleSet.GridCountY));
			for (int32 GridX = 0; GridX < OutSampleSet.GridCountX; ++GridX)
			{
				const float LocalX = FMath::Lerp(
					OutSampleSet.LocalBounds.Min.X,
					OutSampleSet.LocalBounds.Max.X,
					(static_cast<float>(GridX) + 0.5f) / static_cast<float>(OutSampleSet.GridCountX));
				FVector LocalPosition;
				if (!IsDeckSampleSupported(
					*DeckMesh, OutSampleSet.LocalBounds, LocalX, LocalY, Settings, LocalPosition))
				{
					continue;
				}

				FGeneratedDeckSample& Sample = OutSampleSet.Samples.AddDefaulted_GetRef();
				Sample.GridX = GridX;
				Sample.GridY = GridY;
				Sample.LocalPosition = LocalPosition;
			}
		}
		return true;
	}

	AEnemyShip* ResolveDeckSamplingActor(
		AEnemyShip& Context,
		bool& bOutTemporaryActor,
		bool bForceTemporaryActor = false)
	{
		bOutTemporaryActor = false;
		if (!bForceTemporaryActor
			&& Context.GetShipDeckMesh()
			&& Context.GetShipDeckMesh()->IsRegistered())
		{
			return &Context;
		}
		if (!GEditor)
		{
			return nullptr;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.bTemporaryEditorActor = true;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyShip* SamplingActor = EditorWorld->SpawnActor<AEnemyShip>(
			Context.GetClass(), FTransform::Identity, SpawnParameters);
		if (SamplingActor)
		{
			SamplingActor->DeckWaypointGenerationSettings = Context.DeckWaypointGenerationSettings;
			bOutTemporaryActor = true;
		}
		return SamplingActor;
	}

	void GetBlueprintWaypointTemplates(
		USimpleConstructionScript& SCS,
		TArray<UDeckWaypointComponent*>& OutWaypoints)
	{
		OutWaypoints.Reset();
		for (USCS_Node* Node : SCS.GetAllNodes())
		{
			if (UDeckWaypointComponent* Waypoint = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr)
			{
				OutWaypoints.Add(Waypoint);
			}
		}
	}

	bool GenerateDeckWaypointsInBlueprintAsset(AEnemyShip& Context, FString& OutSummary)
	{
		UBlueprint* Blueprint = GetActorBlueprint(Context);
		USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
		AEnemyShip* BlueprintCDO = Blueprint && Blueprint->GeneratedClass
			? Cast<AEnemyShip>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!Blueprint || !SCS || !BlueprintCDO || !BlueprintCDO->GetShipDeckMesh())
		{
			OutSummary = TEXT("Blueprint Asset generation requires an EnemyShip Blueprint with ShipDeckMesh.");
			return false;
		}

		bool bTemporarySamplingActor = false;
		AEnemyShip* SamplingActor = ResolveDeckSamplingActor(Context, bTemporarySamplingActor);
		FGeneratedDeckSampleSet SampleSet;
		if (!SamplingActor || !BuildGeneratedDeckSamples(*SamplingActor, SampleSet))
		{
			if (bTemporarySamplingActor && IsValid(SamplingActor))
			{
				SamplingActor->Destroy();
			}
			OutSummary = TEXT("Could not sample the Blueprint ShipDeckMesh. Check its Static Mesh and query collision.");
			return false;
		}

		const FScopedTransaction Transaction(NSLOCTEXT(
			"DeckWaypointGenerator", "GenerateBlueprintWaypoints", "Generate Deck Waypoints In Blueprint"));
		Blueprint->Modify();
		SCS->Modify();

		TMap<int64, USCS_Node*> ExistingGeneratedByGrid;
		TArray<USCS_Node*> ExistingGeneratedNodes;
		TSet<int32> UsedWaypointIds;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			UDeckWaypointComponent* WaypointTemplate = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr;
			if (!WaypointTemplate)
			{
				continue;
			}
			UsedWaypointIds.Add(WaypointTemplate->GetWaypointId());
			if (WaypointTemplate->WasGeneratedFromDeckMesh())
			{
				ExistingGeneratedNodes.Add(Node);
				const int64 GridKey = MakeDeckGridKey(
					WaypointTemplate->GetGeneratedGridX(), WaypointTemplate->GetGeneratedGridY());
				ExistingGeneratedByGrid.FindOrAdd(GridKey, Node);
			}
		}
		TArray<UDeckWaypointComponent*> RuntimeWaypoints;
		SamplingActor->GetComponents<UDeckWaypointComponent>(RuntimeWaypoints);
		for (const UDeckWaypointComponent* Waypoint : RuntimeWaypoints)
		{
			if (IsValid(Waypoint))
			{
				UsedWaypointIds.Add(Waypoint->GetWaypointId());
			}
		}

		TSet<USCS_Node*> RetainedNodes;
		int32 CreatedCount = 0;
		int32 UpdatedCount = 0;
		for (const FGeneratedDeckSample& Sample : SampleSet.Samples)
		{
			const int64 GridKey = MakeDeckGridKey(Sample.GridX, Sample.GridY);
			USCS_Node* Node = ExistingGeneratedByGrid.FindRef(GridKey);
			UDeckWaypointComponent* WaypointTemplate = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr;
			if (WaypointTemplate)
			{
				Node->Modify();
				WaypointTemplate->Modify();
				if (!WaypointTemplate->IsGeneratedLocationLocked())
				{
					WaypointTemplate->SetRelativeLocation(Sample.LocalPosition);
				}
				WaypointTemplate->InitializeGeneratedWaypoint(
					WaypointTemplate->GetWaypointId(), Sample.GridX, Sample.GridY,
					WaypointTemplate->CanSpawnEnemy(), WaypointTemplate->CanPatrol(),
					WaypointTemplate->CanUseInCombat());
				++UpdatedCount;
			}
			else
			{
				int32 WaypointId = Context.DeckWaypointGenerationSettings.GeneratedWaypointIdBase
					+ Sample.GridY * SampleSet.GridCountX + Sample.GridX;
				while (UsedWaypointIds.Contains(WaypointId) && WaypointId < MAX_int32)
				{
					++WaypointId;
				}
				if (WaypointId == MAX_int32 && UsedWaypointIds.Contains(WaypointId))
				{
					continue;
				}

				const FName VariableName(*FString::Printf(TEXT("GeneratedDeckWaypoint_%d"), WaypointId));
				Node = SCS->CreateNode(UDeckWaypointComponent::StaticClass(), VariableName);
				WaypointTemplate = Node ? Cast<UDeckWaypointComponent>(Node->ComponentTemplate) : nullptr;
				if (!Node || !WaypointTemplate)
				{
					continue;
				}
				WaypointTemplate->SetRelativeLocation(Sample.LocalPosition);
				WaypointTemplate->InitializeGeneratedWaypoint(
					WaypointId, Sample.GridX, Sample.GridY,
					Context.DeckWaypointGenerationSettings.bNewPointsCanSpawn,
					Context.DeckWaypointGenerationSettings.bNewPointsCanPatrol,
					Context.DeckWaypointGenerationSettings.bNewPointsCanUseInCombat);
				SCS->AddNode(Node);
				Node->SetParent(BlueprintCDO->GetShipDeckMesh());
				UsedWaypointIds.Add(WaypointId);
				++CreatedCount;
			}

			RetainedNodes.Add(Node);
		}

		int32 RemovedCount = 0;
		for (USCS_Node* Node : ExistingGeneratedNodes)
		{
			UDeckWaypointComponent* WaypointTemplate = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr;
			if (!WaypointTemplate || RetainedNodes.Contains(Node)
				|| WaypointTemplate->IsGeneratedLocationLocked())
			{
				continue;
			}
			SCS->RemoveNode(Node);
			++RemovedCount;
		}

		TArray<UDeckWaypointComponent*> AllWaypointTemplates;
		GetBlueprintWaypointTemplates(*SCS, AllWaypointTemplates);
		AssignDeckWaypointIds(AllWaypointTemplates, Context.DeckWaypointGenerationSettings);
		RebuildDeckWaypointLinks(
			AllWaypointTemplates,
			*SamplingActor->GetShipDeckMesh(),
			Context.DeckWaypointGenerationSettings);

		if (bTemporarySamplingActor && IsValid(SamplingActor))
		{
			SamplingActor->Destroy();
		}
		OutSummary = FString::Printf(
			TEXT("Blueprint generated %d deck points: %d new, %d updated, %d removed."),
			SampleSet.Samples.Num(), CreatedCount, UpdatedCount, RemovedCount);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return true;
	}

	bool ClearDeckWaypointsInBlueprintAsset(AEnemyShip& Context, FString& OutSummary)
	{
		UBlueprint* Blueprint = GetActorBlueprint(Context);
		USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
		if (!Blueprint || !SCS)
		{
			OutSummary = TEXT("Blueprint Asset clear requires an EnemyShip Blueprint.");
			return false;
		}

		const FScopedTransaction Transaction(NSLOCTEXT(
			"DeckWaypointGenerator", "ClearBlueprintWaypoints", "Clear Generated Deck Waypoints In Blueprint"));
		Blueprint->Modify();
		SCS->Modify();
		TArray<USCS_Node*> NodesToRemove;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			const UDeckWaypointComponent* WaypointTemplate = Node
				? Cast<UDeckWaypointComponent>(Node->ComponentTemplate)
				: nullptr;
			if (WaypointTemplate && WaypointTemplate->WasGeneratedFromDeckMesh())
			{
				NodesToRemove.Add(Node);
			}
		}
		for (USCS_Node* Node : NodesToRemove)
		{
			SCS->RemoveNode(Node);
		}

		OutSummary = FString::Printf(
			TEXT("Removed %d mesh-generated points from the Blueprint. Manual points were preserved."),
			NodesToRemove.Num());
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return true;
	}
#endif
}

AEnemyShip::AEnemyShip()
{
	BossEncounterComponent = CreateDefaultSubobject<UBossEncounterComponent>(TEXT("BossEncounterComponent"));
	DeckEnemySpawnerComponent = CreateDefaultSubobject<UDeckEnemySpawnerComponent>(TEXT("DeckEnemySpawnerComponent"));
	DeckNavigationComponent = CreateDefaultSubobject<UDeckNavigationComponent>(TEXT("DeckNavigationComponent"));
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
	NavigationComponent = CreateDefaultSubobject<UEnemyShipNavigationComponent>(TEXT("EnemyShipNavigationComponent"));
	PatternRuntimeComponent = CreateDefaultSubobject<UEnemyShipPatternRuntimeComponent>(TEXT("EnemyShipPatternRuntimeComponent"));
	EnemyHealthBarComponent = CreateDefaultSubobject<UEnemyHealthBarComponent>(TEXT("EnemyHealthBarComponent"));
	EnemyHealthBarComponent->SetupAttachment(RootComponent);

	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));
	LegacyAbilityBootstrapClasses = {
		UGA_EnemyShipCharge::StaticClass(),
		UGA_EnemyShipLaunchTorpedo::StaticClass(),
		UGA_EnemyShipDeployObstacle::StaticClass(),
		UGA_EnemyShipTimeStop::StaticClass()
	};

	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetCollisionProfileName(TEXT("EnemyShip"));
	}
	if (ShipDamageMesh)
	{
		ShipDamageMesh->SetCollisionProfileName(TEXT("EnemyShipDamage"));
	}
}

void AEnemyShip::BeginPlay()
{
	Super::BeginPlay();
	Tags.Remove(TEXT("Player"));
	Tags.AddUnique(TEXT("Enemy"));

	// HealthComponent를 Ship의 ASC에 바인딩 (BaseEnemy의 패턴과 동일)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
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
		MigrateLegacyNavigationAuthoring();
		if (EnemyShipArchetype)
		{
			EnemyShipArchetype->ApplyToShip(this);
		}
		else if (NavigationComponent)
		{
			// LEGACY: Remove this fallback after every Enemy Ship BP has an Archetype.
			FEnemyShipNavigationProfile LegacyProfile = NavigationComponent->GetNavigationProfile();
			LegacyProfile.IdealDistance = FMath::Max(1.0f, IdealDistance);
			LegacyProfile.ReturnArrivalDistance = FMath::Max(0.0f, NavigationHomeArrivalDistance);
			LegacyProfile.MaxActiveCannons = FMath::Max(1, MaxActiveCannons);
			NavigationComponent->SetNavigationProfile(LegacyProfile);
			GrantEnemyShipAbilityClasses(LegacyAbilityBootstrapClasses);
		}

		if (NavigationComponent)
		{
			NavigationComponent->SetHomeActor(NavigationHomeActor);
		}
	}

	// 캐싱된 대포 목록 탐색
	// Drop에 관한 정보 초기화
	InitializeEnemyDropData();

	// 0.5초마다 타겟과 가장 가까운 N개의 대포를 선정해 목록을 갱신하는 타이머 작동
	if (HasAuthority() && !EnemyShipArchetype && bLegacyAutomaticCannonFireWithoutArchetype)
	{
		GetWorldTimerManager().SetTimer(ActiveCannonsTimerHandle, this, &AEnemyShip::UpdateActiveCannons, 0.5f, true);
	}

	// 군집 서브시스템에 등록
	if (HasAuthority())
	{
		if (DeckEnemySpawnerComponent)
		{
			DeckEnemySpawnerComponent->ConfigureLegacyFallback(
				bEnableDeckEnemyMVP,
				DeckEnemyClass,
				DeckEnemyPoolSize,
				DeckEnemySightActivationDelay,
				DeckEnemyActivationInterval,
				MaxDeckSpawnRetries,
				DeckSpawnRetryInterval,
				DeckEnemyRandomSeed);
		}
		InitializeDeckWaypoints();
		InitializeDeckEnemyPool();

		if (NavigationComponent)
		{
			NavigationComponent->ClearAllOverrides();
			NavigationComponent->SetNavigationEnabled(false);
		}
		if (UShipSwarmSubsystem* SwarmSubsystem = GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
		{
			SwarmSubsystem->RegisterShip(this);
		}
	}
}

void AEnemyShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ABaseEnemy* CrewEnemy : RegisteredCrewEnemies)
	{
		if (IsValid(CrewEnemy))
		{
			CrewEnemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
		}
	}
	if (HasAuthority())
	{
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
	if (DeckNavigationComponent)
	{
		DeckNavigationComponent->RebuildGraph();
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
	if (!HasAuthority() || bDeathHandled
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

bool AEnemyShip::ActivateDeckEnemyAtPoint(
	int32 SpawnPointId,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->ActivateEnemyAtPoint(
			SpawnPointId, InitialTarget, OutEnemy);
}

bool AEnemyShip::ActivateDeckEnemyAtReservation(
	FDeckPointReservation& Reservation,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	if (!DeckEnemySpawnerComponent)
	{
		Reservation.Reset();
		OutEnemy = nullptr;
		return false;
	}
	return DeckEnemySpawnerComponent->ActivateEnemyAtReservation(
		Reservation, InitialTarget, OutEnemy);
}

void AEnemyShip::GenerateDeckWaypointsFromDeckMesh()
{
#if WITH_EDITOR
	if (IsBlueprintAssetAuthoringContext(*this))
	{
		const FString BlueprintClassName = GetNameSafe(GetClass());
		FString GenerationSummary;
		const bool bGenerated = GenerateDeckWaypointsInBlueprintAsset(*this, GenerationSummary);
		if (bGenerated)
		{
			UE_LOG(LogTemp, Display, TEXT("[DeckWaypointGenerator] %s BlueprintClass=%s"),
				*GenerationSummary, *BlueprintClassName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s BlueprintClass=%s"),
				*GenerationSummary, *BlueprintClassName);
		}
		return;
	}
	if (!IsPlacedEditorActor(*this))
	{
		LastDeckWaypointValidationSummary = TEXT("Generation requires a placed EnemyShip actor in an editor level.");
		UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
		return;
	}
	if (!ShipDeckMesh || !ShipDeckMesh->GetStaticMesh())
	{
		LastDeckWaypointValidationSummary = TEXT("ShipDeckMesh has no Static Mesh asset.");
		UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
		return;
	}

	Modify();
	FGeneratedDeckSampleSet SampleSet;
	if (!BuildGeneratedDeckSamples(*this, SampleSet))
	{
		LastDeckWaypointValidationSummary =
			TEXT("Could not sample ShipDeckMesh. Check its Static Mesh, registration, and query collision.");
		UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
		return;
	}
	const FBox& LocalBounds = SampleSet.LocalBounds;
	const int32 GridCountX = SampleSet.GridCountX;

	TArray<UDeckWaypointComponent*> AllWaypoints;
	GetComponents<UDeckWaypointComponent>(AllWaypoints);
	TMap<int64, UDeckWaypointComponent*> ExistingGeneratedByGrid;
	TSet<int32> UsedWaypointIds;
	for (UDeckWaypointComponent* Waypoint : AllWaypoints)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}
		UsedWaypointIds.Add(Waypoint->GetWaypointId());
		if (Waypoint->WasGeneratedFromDeckMesh())
		{
			const int64 GridKey = MakeDeckGridKey(
				Waypoint->GetGeneratedGridX(), Waypoint->GetGeneratedGridY());
			if (!ExistingGeneratedByGrid.Contains(GridKey))
			{
				ExistingGeneratedByGrid.Add(GridKey, Waypoint);
			}
		}
	}

	TSet<UDeckWaypointComponent*> RetainedWaypoints;
	int32 CreatedCount = 0;
	int32 UpdatedCount = 0;
	for (const FGeneratedDeckSample& Sample : SampleSet.Samples)
	{
		const int64 GridKey = MakeDeckGridKey(Sample.GridX, Sample.GridY);
		UDeckWaypointComponent* Waypoint = ExistingGeneratedByGrid.FindRef(GridKey);
		if (Waypoint)
		{
			Waypoint->Modify();
			if (!Waypoint->IsGeneratedLocationLocked())
			{
				Waypoint->AttachToComponent(ShipDeckMesh, FAttachmentTransformRules::KeepRelativeTransform);
				Waypoint->SetRelativeLocation(Sample.LocalPosition);
			}
			Waypoint->InitializeGeneratedWaypoint(
				Waypoint->GetWaypointId(), Sample.GridX, Sample.GridY,
				Waypoint->CanSpawnEnemy(), Waypoint->CanPatrol(), Waypoint->CanUseInCombat());
			++UpdatedCount;
		}
		else
		{
			int32 WaypointId = DeckWaypointGenerationSettings.GeneratedWaypointIdBase
				+ Sample.GridY * GridCountX + Sample.GridX;
			while (UsedWaypointIds.Contains(WaypointId) && WaypointId < MAX_int32)
			{
				++WaypointId;
			}
			if (WaypointId == MAX_int32 && UsedWaypointIds.Contains(WaypointId))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[DeckWaypointGenerator] No free WaypointId remains. Ship=%s"), *GetName());
				continue;
			}

			const FName ComponentName = MakeUniqueObjectName(
				this,
				UDeckWaypointComponent::StaticClass(),
				FName(*FString::Printf(TEXT("GeneratedDeckWaypoint_%d"), WaypointId)));
			Waypoint = NewObject<UDeckWaypointComponent>(
				this, UDeckWaypointComponent::StaticClass(), ComponentName, RF_Transactional);
			if (!Waypoint)
			{
				continue;
			}

			AddInstanceComponent(Waypoint);
			Waypoint->OnComponentCreated();
			Waypoint->SetupAttachment(ShipDeckMesh);
			Waypoint->SetRelativeLocation(Sample.LocalPosition);
			Waypoint->InitializeGeneratedWaypoint(
				WaypointId, Sample.GridX, Sample.GridY,
				DeckWaypointGenerationSettings.bNewPointsCanSpawn,
				DeckWaypointGenerationSettings.bNewPointsCanPatrol,
				DeckWaypointGenerationSettings.bNewPointsCanUseInCombat);
			Waypoint->RegisterComponent();
			UsedWaypointIds.Add(WaypointId);
			++CreatedCount;
		}

		RetainedWaypoints.Add(Waypoint);
	}

	int32 RemovedCount = 0;
	for (UDeckWaypointComponent* Waypoint : AllWaypoints)
	{
		if (!IsValid(Waypoint) || !Waypoint->WasGeneratedFromDeckMesh()
			|| RetainedWaypoints.Contains(Waypoint) || Waypoint->IsGeneratedLocationLocked())
		{
			continue;
		}
		Waypoint->Modify();
		Waypoint->DestroyComponent();
		++RemovedCount;
	}

	TArray<UDeckWaypointComponent*> ConfiguredWaypoints;
	GetComponents<UDeckWaypointComponent>(ConfiguredWaypoints);
	AssignDeckWaypointIds(ConfiguredWaypoints, DeckWaypointGenerationSettings);
	RebuildDeckWaypointLinks(ConfiguredWaypoints, *ShipDeckMesh, DeckWaypointGenerationSettings);

	MarkPackageDirty();
	LastDeckWaypointValidationSummary = FString::Printf(
		TEXT("Generated %d deck points: %d new, %d updated, %d removed. Existing usage flags were preserved."),
		SampleSet.Samples.Num(), CreatedCount, UpdatedCount, RemovedCount);
	UE_LOG(LogTemp, Display, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
		*LastDeckWaypointValidationSummary, *GetName());
	ValidateDeckWaypoints();
#else
	LastDeckWaypointValidationSummary = TEXT("Deck waypoint generation is editor-only.");
#endif
}

void AEnemyShip::ClearGeneratedDeckWaypoints()
{
#if WITH_EDITOR
	if (IsBlueprintAssetAuthoringContext(*this))
	{
		const FString BlueprintClassName = GetNameSafe(GetClass());
		FString ClearSummary;
		const bool bCleared = ClearDeckWaypointsInBlueprintAsset(*this, ClearSummary);
		if (bCleared)
		{
			UE_LOG(LogTemp, Display, TEXT("[DeckWaypointGenerator] %s BlueprintClass=%s"),
				*ClearSummary, *BlueprintClassName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s BlueprintClass=%s"),
				*ClearSummary, *BlueprintClassName);
		}
		return;
	}
	if (!IsPlacedEditorActor(*this))
	{
		LastDeckWaypointValidationSummary = TEXT("Clear requires a placed EnemyShip actor in an editor level.");
		return;
	}

	Modify();
	TArray<UDeckWaypointComponent*> Waypoints;
	GetComponents<UDeckWaypointComponent>(Waypoints);
	int32 RemovedCount = 0;
	for (UDeckWaypointComponent* Waypoint : Waypoints)
	{
		if (IsValid(Waypoint) && Waypoint->WasGeneratedFromDeckMesh())
		{
			Waypoint->Modify();
			Waypoint->DestroyComponent();
			++RemovedCount;
		}
	}
	MarkPackageDirty();
	LastDeckWaypointValidationSummary = FString::Printf(
		TEXT("Removed %d mesh-generated deck points. Manual points were preserved."), RemovedCount);
	UE_LOG(LogTemp, Display, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
		*LastDeckWaypointValidationSummary, *GetName());
#else
	LastDeckWaypointValidationSummary = TEXT("Deck waypoint generation is editor-only.");
#endif
}

void AEnemyShip::ValidateDeckWaypoints()
{
#if WITH_EDITOR
	if (IsBlueprintAssetAuthoringContext(*this))
	{
		bool bTemporaryValidationActor = false;
		AEnemyShip* ValidationActor = ResolveDeckSamplingActor(
			*this, bTemporaryValidationActor, true);
		if (!ValidationActor)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DeckWaypointGenerator] Could not create a temporary Blueprint validation actor. Class=%s"),
				*GetNameSafe(GetClass()));
			return;
		}
		ValidationActor->ValidateDeckWaypoints();
		if (bTemporaryValidationActor && IsValid(ValidationActor))
		{
			ValidationActor->Destroy();
		}
		return;
	}
	if (!ShipDeckMesh || !ShipDeckMesh->GetStaticMesh())
	{
		LastDeckWaypointValidationSummary = TEXT("Validation failed: ShipDeckMesh has no Static Mesh asset.");
		UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
		return;
	}

	TArray<UDeckWaypointComponent*> Waypoints;
	GetComponents<UDeckWaypointComponent>(Waypoints);
	TMap<int32, UDeckWaypointComponent*> WaypointById;
	int32 ErrorCount = 0;
	int32 CombatCount = 0;
	int32 ExcludedCount = 0;
	for (UDeckWaypointComponent* Waypoint : Waypoints)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}
		Waypoint->RefreshEditorVisualization();
		if (WaypointById.Contains(Waypoint->GetWaypointId()))
		{
			++ErrorCount;
			UE_LOG(LogTemp, Error,
				TEXT("[DeckWaypointGenerator] Duplicate WaypointId=%d Ship=%s Component=%s"),
				Waypoint->GetWaypointId(), *GetName(), *GetNameSafe(Waypoint));
		}
		else
		{
			WaypointById.Add(Waypoint->GetWaypointId(), Waypoint);
		}
		if (!Waypoint->IsAttachedTo(ShipDeckMesh))
		{
			++ErrorCount;
			UE_LOG(LogTemp, Error,
				TEXT("[DeckWaypointGenerator] Point is not attached below ShipDeckMesh. Ship=%s Component=%s"),
				*GetName(), *GetNameSafe(Waypoint));
		}
		CombatCount += Waypoint->CanUseInCombat() ? 1 : 0;
		ExcludedCount += (!Waypoint->CanUseInCombat() && !Waypoint->CanPatrol()) ? 1 : 0;
	}

	const FBox LocalBounds = ShipDeckMesh->GetStaticMesh()->GetBoundingBox();
	for (UDeckWaypointComponent* Waypoint : Waypoints)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}
		int32 ValidLinkedCount = 0;
		for (const int32 LinkedId : Waypoint->GetLinkedWaypointIds())
		{
			if (LinkedId == Waypoint->GetWaypointId())
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error,
					TEXT("[DeckWaypointGenerator] Self link. Ship=%s WaypointId=%d"),
					*GetName(), Waypoint->GetWaypointId());
				continue;
			}
			UDeckWaypointComponent* const* LinkedWaypoint = WaypointById.Find(LinkedId);
			if (!LinkedWaypoint)
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error,
					TEXT("[DeckWaypointGenerator] Invalid link. Ship=%s WaypointId=%d LinkedId=%d"),
					*GetName(), Waypoint->GetWaypointId(), LinkedId);
				continue;
			}
			++ValidLinkedCount;
			if (!(*LinkedWaypoint)->GetLinkedWaypointIds().Contains(Waypoint->GetWaypointId()))
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error,
					TEXT("[DeckWaypointGenerator] One-way link. Ship=%s WaypointId=%d LinkedId=%d"),
					*GetName(), Waypoint->GetWaypointId(), LinkedId);
			}
		}
		if ((Waypoint->CanPatrol() || Waypoint->CanUseInCombat()) && ValidLinkedCount == 0)
		{
			++ErrorCount;
			UE_LOG(LogTemp, Error,
				TEXT("[DeckWaypointGenerator] Usable point is isolated. Ship=%s WaypointId=%d"),
				*GetName(), Waypoint->GetWaypointId());
		}
		if (Waypoint->CanSpawnEnemy()
			&& (!Waypoint->CanUseInCombat() || ValidLinkedCount == 0))
		{
			++ErrorCount;
			UE_LOG(LogTemp, Error,
				TEXT("[DeckWaypointGenerator] Spawn point requires Combat usage and at least one valid link. Ship=%s WaypointId=%d"),
				*GetName(), Waypoint->GetWaypointId());
		}

		if (Waypoint->WasGeneratedFromDeckMesh())
		{
			const FVector LocalLocation = Waypoint->GetRelativeLocation();
			FVector SupportedLocation;
			const bool bSupported = IsDeckSampleSupported(
				*ShipDeckMesh, LocalBounds, LocalLocation.X, LocalLocation.Y,
				DeckWaypointGenerationSettings, SupportedLocation);
			float SurfaceDistance = TNumericLimits<float>::Max();
			if (bSupported)
			{
				const FTransform DeckTransform = ShipDeckMesh->GetComponentTransform();
				SurfaceDistance = FVector::Distance(
					DeckTransform.TransformPosition(LocalLocation),
					DeckTransform.TransformPosition(SupportedLocation));
			}
			if (!bSupported || SurfaceDistance > FMath::Max(10.0f, DeckWaypointGenerationSettings.MaximumStepHeight))
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error,
					TEXT("[DeckWaypointGenerator] Generated point is no longer safely supported by the deck. Ship=%s WaypointId=%d"),
					*GetName(), Waypoint->GetWaypointId());
			}
		}
	}

	LastDeckWaypointValidationSummary = FString::Printf(
		TEXT("Validated %d points: %d combat, %d explicitly excluded, %d errors."),
		WaypointById.Num(), CombatCount, ExcludedCount, ErrorCount);
	if (ErrorCount == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DeckWaypointGenerator] %s Ship=%s"),
			*LastDeckWaypointValidationSummary, *GetName());
	}
#else
	LastDeckWaypointValidationSummary = TEXT("Deck waypoint validation is editor-only.");
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

bool AEnemyShip::ResolveFixedDeckAnchorTransform(
	int32 WaypointId,
	float CapsuleHalfHeight,
	FTransform& OutTransform) const
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->ResolveFixedDeckAnchorTransform(
			WaypointId, CapsuleHalfHeight, OutTransform);
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

void AEnemyShip::GetDeckWaypointIds(
	TArray<int32>& OutWaypointIds,
	bool bRequireCombatPoint) const
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->GetWaypointIds(OutWaypointIds, bRequireCombatPoint);
	}
	else
	{
		OutWaypointIds.Reset();
	}
}

void AEnemyShip::GetConnectedDeckWaypointIds(
	int32 WaypointId,
	TArray<int32>& OutWaypointIds) const
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->GetConnectedWaypointIds(WaypointId, OutWaypointIds);
	}
	else
	{
		OutWaypointIds.Reset();
	}
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

bool AEnemyShip::IsDeckCombatPointClaimAvailable(
	int32 WaypointId,
	const AActor* Requester) const
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->IsCombatPointClaimAvailable(WaypointId, Requester);
}

bool AEnemyShip::TryClaimDeckCombatPoint(int32 WaypointId, AActor* Requester)
{
	return DeckEnemySpawnerComponent
		&& DeckEnemySpawnerComponent->TryClaimCombatPoint(WaypointId, Requester);
}

void AEnemyShip::ReleaseDeckCombatPointClaim(int32 WaypointId, AActor* Requester)
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ReleaseCombatPointClaim(WaypointId, Requester);
	}
}

void AEnemyShip::ReleaseAllDeckPointsFor(AActor* Actor)
{
	if (DeckEnemySpawnerComponent)
	{
		DeckEnemySpawnerComponent->ReleaseAllPointsFor(Actor);
	}
}

int32 AEnemyShip::FindNearestDeckWaypoint(
	const FVector& WorldLocation,
	bool bRequirePatrolPoint) const
{
	return DeckEnemySpawnerComponent
		? DeckEnemySpawnerComponent->FindNearestWaypoint(WorldLocation, bRequirePatrolPoint)
		: INDEX_NONE;
}

void AEnemyShip::MigrateLegacyNavigationAuthoring()
{
	// LEGACY: One-time bridge for old BP-authored ReturnPointActor and
	// ReturnArrivalOffset variables. Delete after content migration M11.
	if (!NavigationHomeActor)
	{
		if (const FObjectPropertyBase* ReturnPointProperty = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("ReturnPointActor")))
		{
			NavigationHomeActor = Cast<AActor>(ReturnPointProperty->GetObjectPropertyValue_InContainer(this));
		}
	}

	if (const FNumericProperty* ArrivalOffsetProperty = FindFProperty<FNumericProperty>(GetClass(), TEXT("ReturnArrivalOffset")))
	{
		const void* ValueAddress = ArrivalOffsetProperty->ContainerPtrToValuePtr<void>(this);
		const float LegacyDistance = static_cast<float>(ArrivalOffsetProperty->GetFloatingPointPropertyValue(ValueAddress));
		if (LegacyDistance > 0.0f)
		{
			NavigationHomeArrivalDistance = LegacyDistance;
		}
	}
}

bool AEnemyShip::GrantEnemyShipAbilities(const UEnemyShipAbilitySet* AbilitySet)
{
	if (!HasAuthority() || !AbilitySet)
	{
		return false;
	}
	return GrantEnemyShipAbilityClasses(AbilitySet->Abilities);
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

bool AEnemyShip::ConfigureEnemyShipPattern(UEnemyShipPatternData* Pattern)
{
	if (!HasAuthority() || !Pattern || !NavigationComponent || !PatternRuntimeComponent)
	{
		return false;
	}

	TArray<UEnemyShipSkillModuleData*> RawCoreModules;
	for (UEnemyShipSkillModuleData* Module : CoreSkillModules)
	{
		if (IsValid(Module))
		{
			RawCoreModules.AddUnique(Module);
		}
	}
	PatternRuntimeComponent->SetCoreSkillModules(RawCoreModules);
	PatternRuntimeComponent->SetPattern(Pattern);
	NavigationComponent->SetNavigationProfile(Pattern->NavigationProfile);

	TArray<TSubclassOf<UGameplayAbility>> AbilityClasses;
	TSet<const UEnemyShipSkillModuleData*> SeenModules;
	auto AppendModuleAbilities = [&AbilityClasses, &SeenModules](const UEnemyShipSkillModuleData* Module)
	{
		if (!IsValid(Module) || SeenModules.Contains(Module) || !Module->AbilitySet)
		{
			return;
		}
		SeenModules.Add(Module);
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : Module->AbilitySet->Abilities)
		{
			AbilityClasses.AddUnique(AbilityClass);
		}
	};
	for (const UEnemyShipSkillModuleData* Module : CoreSkillModules)
	{
		AppendModuleAbilities(Module);
	}
	for (const UEnemyShipSkillModuleData* Module : Pattern->SkillModules)
	{
		AppendModuleAbilities(Module);
	}
	return GrantEnemyShipAbilityClasses(AbilityClasses);
}

void AEnemyShip::SetCoreSkillModules(const TArray<UEnemyShipSkillModuleData*>& InCoreModules)
{
	CoreSkillModules.Reset();
	for (UEnemyShipSkillModuleData* Module : InCoreModules)
	{
		if (IsValid(Module))
		{
			CoreSkillModules.AddUnique(Module);
		}
	}
}

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	EvaluateCrewControlState();

	if (CVarShowEnemyShipAIDebug.GetValueOnGameThread() > 0)
	{
		DrawEnemyShipAIDebug();
	}

	if (HasAuthority() && !bCrewDefeated && !bDeathHandled && !EnemyShipArchetype
		&& bLegacyAutomaticCannonFireWithoutArchetype)
	{
		TickAIAimingAndFiring(DeltaTime);
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

	DrawRange(Center, Profile.DangerCloseDistance, FColor::Red, TEXT("DangerClose"));
	DrawRange(Center, Profile.IdealDistance, FColor::Green, TEXT("Ideal"));
	DrawRange(
		Center,
		Profile.IdealDistance + Profile.OrbitTolerance,
		FColor::Yellow,
		TEXT("OrbitMax"));
	DrawRange(Center, Profile.DetectionDistance, FColor::Cyan, TEXT("Detection"));

	if (const AActor* HomeActor = NavigationComponent->GetHomeActor())
	{
		const FVector HomeCenter = HomeActor->GetActorLocation() + FVector(0.0f, 0.0f, HeightOffset);
		DrawRange(HomeCenter, Profile.ReturnArrivalDistance, FColor::Magenta, TEXT("ReturnArrival"));
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
	const UEnemyShipPatternData* Pattern = PatternRuntimeComponent
		? PatternRuntimeComponent->GetPattern()
		: nullptr;
	if (!Pattern && EnemyShipArchetype)
	{
		Pattern = EnemyShipArchetype->Pattern;
	}
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
		TEXT("%s [%s]\nCASTING: %s\nNav=%s State=%s Override=%s\nTarget=%s Dist=%s\nPattern=%s Rules=%d"),
		*GetName(),
		HasAuthority() ? TEXT("AUTH") : TEXT("CLIENT"),
		*CastingSummary,
		NavigationComponent->IsNavigationEnabled() ? TEXT("ON") : TEXT("OFF"),
		*StateName,
		NavigationComponent->HasActiveOverride() ? TEXT("YES") : TEXT("NO"),
		TargetShip ? *TargetShip->GetName() : TEXT("None"),
		TargetDistance >= 0.0f ? *FString::Printf(TEXT("%.0fcm"), TargetDistance) : TEXT("-"),
		Pattern ? *Pattern->GetName() : TEXT("None"),
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
		HandleShipDeath();
	}
}

void AEnemyShip::HandleShipDeath()
{
	if (!HasAuthority()) return;
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
	GetWorldTimerManager().ClearTimer(ActiveCannonsTimerHandle);
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			Cannon->SetAIAimRotation(0.0f, 0.0f);
		}
	}
	ActiveAICannons.Empty();

	DropAtDeathLocation(DeathLocation, DeathRotation);

	// 5. Player ships and enemy ships share the exact buoyancy-off/destruction path.
	StartSinking(DestroyAfterDeathDelay);
}

void AEnemyShip::InitializeEnemyDropData()
{
	// Drop 할 아이템을 Data Table에서 가져오기
	EnemyDropData = FEnemyDropData();
	if (!EnemyDropDataTable || !EnemyTypeTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::InitializeEnemyDropData - Missing drop setup. Ship=%s DropTable=%s EnemyTypeTag=%s"),
			*GetName(),
			*GetNameSafe(EnemyDropDataTable),
			*EnemyTypeTag.ToString());
		return;
	}

	static const FString ContextString(TEXT("EnemyShipDropData"));
	TArray<FEnemyDropDataRow*> Rows;
	EnemyDropDataTable->GetAllRows(ContextString, Rows);

	for (const FEnemyDropDataRow* Row : Rows)
	{
		if (!Row || Row->EnemyTag != EnemyTypeTag)
		{
			continue;
		}

		EnemyDropData.EnemyTag = Row->EnemyTag;
		EnemyDropData.DropEntries = Row->DropEntries;
		UE_LOG(LogTemp, Log, TEXT("AEnemyShip::InitializeEnemyDropData - Loaded %d drop entries. Ship=%s EnemyTypeTag=%s"),
			EnemyDropData.DropEntries.Num(),
			*GetName(),
			*EnemyTypeTag.ToString());
		break;
	}

	if (EnemyDropData.DropEntries.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::InitializeEnemyDropData - No matching row or empty drop entries. Ship=%s EnemyTypeTag=%s Table=%s"),
			*GetName(),
			*EnemyTypeTag.ToString(),
			*GetNameSafe(EnemyDropDataTable));
	}
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

	// 1. 레벨에서 지정한 상자 정의 DataAsset이 있는 경우 (데이터 기반 스폰)
	if (IsValid(SunkChestDefinition))
	{
		TSubclassOf<AStorageChest> ChestClassToSpawn = SunkChestDefinition->ChestClass ? SunkChestDefinition->ChestClass : EnemyCorpseStorageClass;
		if (!ChestClassToSpawn)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: SunkChestDefinition and EnemyCorpseStorageClass are both missing valid ChestClass."), *GetName());
			return;
		}

		AStorageChest* SpawnedStorage = World->SpawnActorDeferred<AStorageChest>(
			ChestClassToSpawn,
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
		);

		if (SpawnedStorage)
		{
			const int32 DropSeed = FMath::RandRange(1, MAX_int32);
			SpawnedStorage->InitializeFromChestDefinition(SunkChestDefinition, DropSeed);
			SpawnedStorage->SetPhysicsAndBuoyancyEnabled(true);
			SpawnedStorage->FinishSpawning(SpawnTransform);
			SpawnedStorage->ForceNetUpdate();

			UE_LOG(LogTemp, Log, TEXT("AEnemyShip::DropAtDeathLocation - Spawned buoyant chest from SunkChestDefinition (%s). Ship=%s Location=%s"),
				*GetNameSafe(SunkChestDefinition),
				*GetName(),
				*SpawnedStorage->GetActorLocation().ToString());
			return;
		}
	}

	// 2. 레거시/기존 구조체 기반 드랍 Fallback
	if (!EnemyCorpseStorageClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: EnemyCorpseStorageClass is not configured."), *GetName());
		return;
	}

	// Storage에 들어갈 아이템들의 배열 생성
	TArray<FStorageItemEntry> StorageItems;
	StorageItems.Reserve(EnemyDropData.DropEntries.Num());

	int32 InvalidEntryCount = 0;
	int32 FailedChanceCount = 0;

	// 한 row에 있는 아이템 마다 반복
	for (const FEnemyDropEntry& Entry : EnemyDropData.DropEntries)
	{
		if (!Entry.ItemTag.IsValid())
		{
			++InvalidEntryCount;
			continue;
		}

		const float ClampedChance = FMath::Clamp(Entry.DropChance, 0.f, 1.f);
		// 랜덤으로 뽑은 값이 확률보다 크면 Spawn 하지 않음, Guaranteed면 무조건 Spawn
		if (!Entry.bGuaranteed && FMath::FRand() > ClampedChance)
		{
			++FailedChanceCount;
			continue;
		}

		const int32 MinCount = FMath::Max(1, Entry.MinCount);
		const int32 MaxCount = FMath::Max(MinCount, Entry.MaxCount);

		FStorageItemEntry& StorageItem = StorageItems.AddDefaulted_GetRef();
		StorageItem.ItemTag = Entry.ItemTag;
		StorageItem.Count = FMath::RandRange(MinCount, MaxCount);
	}

	if (StorageItems.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - No storage items selected, so chest will not spawn. Ship=%s EnemyTypeTag=%s Entries=%d Invalid=%d FailedChance=%d"),
			*GetName(),
			*EnemyTypeTag.ToString(),
			EnemyDropData.DropEntries.Num(),
			InvalidEntryCount,
			FailedChanceCount);
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AStorageChest* SpawnedStorage = World->SpawnActor<AStorageChest>(
		EnemyCorpseStorageClass,
		SpawnTransform,
		SpawnParameters
	);

	if (SpawnedStorage)
	{
		SpawnedStorage->SetReplicates(true);
		SpawnedStorage->SetReplicateMovement(true);
		SpawnedStorage->bAlwaysRelevant = true;
		SpawnedStorage->SetNetCullDistanceSquared(FMath::Square(500000.0f));
		SpawnedStorage->SetOwner(nullptr);
		SpawnedStorage->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SpawnedStorage->SetLifeSpan(0.0f);
		SpawnedStorage->SetPhysicsAndBuoyancyEnabled(true);

		TMap<FGameplayTag, int32> TotalCountByItem;
		for (const FStorageItemEntry& StorageItem : StorageItems)
		{
			// map에 아이템 태그랑 개수 추가 
			TotalCountByItem.FindOrAdd(StorageItem.ItemTag) += StorageItem.Count;
		}

		// 앞서 구했던 아이템 개수만큼 슬롯 추가
		int32 RequiredSlotCount = StorageItems.Num();
		if (const UStorageComponent* StorageComponent = SpawnedStorage->GetStorageComponent())
		{
			RequiredSlotCount = 0;
			for (const TPair<FGameplayTag, int32>& ItemTotal : TotalCountByItem)
			{
				// map에 저장된 정보에서, 최대 스택보다 많은 수가 있으면 slot 분할
				const int32 MaxStack = FMath::Max(1, StorageComponent->GetMaxStack(ItemTotal.Key));
				RequiredSlotCount += FMath::DivideAndRoundUp(ItemTotal.Value, MaxStack);
			}
		}

		const int32 SlotCount = FMath::Max(EnemyCorpseStorageSlotCount, RequiredSlotCount);
		SpawnedStorage->ConfigureStorage(SlotCount, EnemyCorpseStorageColumnCount, StorageItems);
		// Replicate the fully configured storage contents in the same server update as the spawn.
		SpawnedStorage->ForceNetUpdate();
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - Spawned storage chest. Ship=%s Chest=%s Location=%s Items=%d Slots=%d"),
			*GetName(),
			*GetNameSafe(SpawnedStorage),
			*SpawnedStorage->GetActorLocation().ToString(),
			StorageItems.Num(),
			SlotCount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::DropAtDeathLocation - SpawnActor failed. Ship=%s StorageClass=%s Location=%s"),
			*GetName(),
			*GetNameSafe(EnemyCorpseStorageClass),
			*SpawnLocation.ToString());
	}
}

void AEnemyShip::UpdateActiveCannons()
{
	if (!HasAuthority()) return;

	TArray<ACannon*> AvailableCannons;
	AvailableCannons.Reserve(MountedCannons.Num());
	for (ACannon* Cannon : MountedCannons)
	{
		if (IsValid(Cannon))
		{
			AvailableCannons.AddUnique(Cannon);
		}
	}

	if (!IsValid(AITargetShip) || AvailableCannons.IsEmpty())
	{
		// 타겟이 없거나 대포가 없으면 활성 대포 정렬을 비우고 기존 대포는 정렬 리셋
		ActiveAICannons.Empty();
		for (ACannon* Cannon : AvailableCannons)
		{
			if (Cannon)
			{
				Cannon->SetAIAimRotation(0.f, 0.f);
			}
		}
		return;
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 타겟 선박과의 거리 기준 정렬 (제곱 거리로 연산 최소화)
	TArray<ACannon*> SortedCannons = MoveTemp(AvailableCannons);
	SortedCannons.Sort([TargetLoc](const ACannon& A, const ACannon& B) {
		float DistA = FVector::DistSquared(A.GetActorLocation(), TargetLoc);
		float DistB = FVector::DistSquared(B.GetActorLocation(), TargetLoc);
		return DistA < DistB;
	});

	ActiveAICannons.Empty();
	const int32 CountToSelect = FMath::Clamp(MaxActiveCannons, 0, SortedCannons.Num());
	for (int32 i = 0; i < CountToSelect; ++i)
	{
		ActiveAICannons.Add(SortedCannons[i]);
	}

	// 활성화되지 못한 나머지 대포들은 조준 초기화(정면 복귀)
	for (ACannon* Cannon : MountedCannons)
	{
		if (Cannon && !ActiveAICannons.Contains(Cannon))
		{
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
	}
}

void AEnemyShip::TickAIAimingAndFiring(float DeltaTime)
{
	if (!AITargetShip || ActiveAICannons.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const float Gravity = FMath::Abs(World->GetGravityZ());
	if (Gravity <= 0.01f)
	{
		return; // 비정상 물리 상태 예외 처리
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 2. 활성 대포별로 각각 조준각 연산 및 발사 진행
	for (ACannon* Cannon : ActiveAICannons)
	{
		if (!IsValid(Cannon)) continue;

		const float ProjectileSpeed = Cannon->GetResolvedFiringStats().ProjectileSpeed;
		if (ProjectileSpeed <= 10.0f)
		{
			Cannon->SetAIAimRotation(0.0f, 0.0f);
			continue;
		}

		FVector StartLoc = Cannon->GetActorLocation();
		FVector ToTarget = TargetLoc - StartLoc;

		float HorizDist = FVector::Dist2D(StartLoc, TargetLoc);
		float VertDist = ToTarget.Z;

		// 3. 탄도학 투사 궤적 공식 대입 (해석학적 공식)
		// Disc = v^4 - g * (g * x^2 + 2 * y * v^2)
		float SpeedSq = ProjectileSpeed * ProjectileSpeed;
		float Speed4 = SpeedSq * SpeedSq;
		float Disc = Speed4 - Gravity * (Gravity * HorizDist * HorizDist + 2.f * VertDist * SpeedSq);

		if (Disc < 0.f)
		{
			// 최대 사거리를 벗어난 경우 조준을 풀고 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
			continue;
		}

		// 저각 탄도 계산
		float PitchRad = FMath::Atan2(SpeedSq - FMath::Sqrt(Disc), Gravity * HorizDist);
		// 월드 공간 발사 방향 벡터 생성
		FVector HorizDir = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal();
		FVector LaunchDir = HorizDir * FMath::Cos(PitchRad) + FVector(0.f, 0.f, FMath::Sin(PitchRad));

		// 대포의 로컬 공간으로 변환하여 Yaw / Pitch 도출
		FVector LocalLaunchDir = Cannon->GetActorTransform().InverseTransformVector(LaunchDir);
		FRotator TargetRot = LocalLaunchDir.Rotation();

		float TargetPitch = TargetRot.Pitch;
		float TargetYaw = TargetRot.Yaw;

		// 4. 180도 고개 돌림 방지 체크 (로컬 Yaw가 좌우 90도를 초과하면 조준 불가 상태 처리)
		if (FMath::Abs(TargetYaw) > 90.f)
		{
			// 조준하지 않고 정면 정렬 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
		else
		{
			// 조준 제어 적용
			Cannon->SetAIAimRotation(TargetPitch, TargetYaw);

			// 선회(Orbit) 또는 도망(Retreat) 상태 시 지속 발사
			if (CurrentCombatState == ENavalCombatState::Orbit || CurrentCombatState == ENavalCombatState::Retreat)
			{
				Cannon->FireCannon();
			}
		}
	}
}

bool AEnemyShip::AllowsPlayerAnchorControl(AActor* Interactor) const
{
	return !bDeathHandled && (bCrewDefeated || !HasLivingCrew());
}

int32 AEnemyShip::GetLivingCrewCount() const
{
	int32 Count = 0;

	// 1. Registered manual/external crew enemies
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

	// 2. DeckEnemySpawnerComponent-owned crew (active and not-yet-deployed pool members)
	if (DeckEnemySpawnerComponent)
	{
		Count += DeckEnemySpawnerComponent->GetLivingPooledEnemyCount();
	}

	// 3. Boss Encounter Component
	if (BossEncounterComponent && BossEncounterComponent->IsEncounterEnabled())
	{
		const EBossEncounterState State = BossEncounterComponent->GetEncounterState();
		if (State == EBossEncounterState::Active || State == EBossEncounterState::Spawning || State == EBossEncounterState::Waiting)
		{
			if (const AShipBossEnemy* Boss = BossEncounterComponent->GetSpawnedBoss())
			{
				if (const UBaseHealthComponent* Health = Boss->GetHealthComponent())
				{
					if (!Health->IsDead())
					{
						++Count;
					}
				}
			}
			else
			{
				++Count;
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
	if (IsValid(CrewEnemy))
	{
		RegisteredCrewEnemies.AddUnique(CrewEnemy);
		CrewEnemy->OnBaseEnemyDeathNotified.AddUniqueDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
		EvaluateCrewControlState();
	}
}

void AEnemyShip::UnregisterCrewEnemy(ABaseEnemy* CrewEnemy)
{
	if (CrewEnemy)
	{
		CrewEnemy->OnBaseEnemyDeathNotified.RemoveDynamic(this, &AEnemyShip::HandleCrewEnemyRemoved);
		RegisteredCrewEnemies.Remove(CrewEnemy);
		EvaluateCrewControlState();
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
		if (bCrewDefeated)
		{
			bCrewDefeated = false;
			OnRep_CrewDefeated();
			ForceNetUpdate();
		}
		return;
	}
	if (bCrewDefeated)
	{
		return;
	}

	bCrewDefeated = true;
	DisableEnemyShipAIForCapture();
	OnRep_CrewDefeated();
	ForceNetUpdate();
}

void AEnemyShip::DisableEnemyShipAIForCapture()
{
	SetAIControlInput(0.0f, 0.0f);
	AITargetShip = nullptr;
	CurrentCombatState = ENavalCombatState::Idle;
	GetWorldTimerManager().ClearTimer(ActiveCannonsTimerHandle);
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
			Cannon->SetAIAimRotation(0.0f, 0.0f);
		}
	}
	ActiveAICannons.Reset();
}

void AEnemyShip::OnRep_CrewDefeated()
{
	UpdateHelmInteractionAvailability();
}

void AEnemyShip::HandleCrewEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason)
{
	EvaluateCrewControlState();
}

void AEnemyShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShip, bCrewDefeated);
}
