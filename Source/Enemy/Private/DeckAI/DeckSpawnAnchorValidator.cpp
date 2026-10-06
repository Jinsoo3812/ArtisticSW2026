#include "DeckAI/DeckSpawnAnchorValidator.h"

#if WITH_EDITOR
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Editor.h"
#include "EnemyBalanceData.h"
#include "Engine/World.h"
#include "ShipAI/EnemyShip.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeckSpawnAnchorValidation, Log, All);

FString FDeckSpawnAnchorValidationResult::ToSummary() const
{
	return FString::Printf(TEXT("Validated %d spawn anchors (%d total points): %d errors. %s"),
		AnchorCount, PointCount, ErrorCount, bCheckedRuntimeFloor
		? TEXT("Server floor and clearance checked.")
		: TEXT("Static configuration only; confirm floor and clearance in server PIE."));
}

FDeckSpawnAnchorValidationResult FDeckSpawnAnchorValidator::Validate(AEnemyShip& Context)
{
	AEnemyShip* ValidationShip = &Context;
	const UWorld* World = Context.GetWorld();
	const bool bNeedsTemporaryActor = Context.HasAnyFlags(RF_ClassDefaultObject)
		|| (World && World->WorldType == EWorldType::EditorPreview);
	if (bNeedsTemporaryActor)
	{
		UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.bTemporaryEditorActor = true;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ValidationShip = EditorWorld
			? EditorWorld->SpawnActor<AEnemyShip>(Context.GetClass(), FTransform::Identity, Params) : nullptr;
	}
	if (!ValidationShip)
	{
		FDeckSpawnAnchorValidationResult Failed;
		Failed.ErrorCount = 1;
		UE_LOG(LogDeckSpawnAnchorValidation, Error,
			TEXT("[DeckSpawnAnchorValidation] Class=%s Reason=MissingEditorValidationWorld"),
			*GetNameSafe(Context.GetClass()));
		return Failed;
	}

	FDeckSpawnAnchorValidator Validator(*ValidationShip);
	Validator.CollectPoints();
	Validator.ValidateWalkArea();
	Validator.ValidateSpawnPlan();
	Validator.ValidateBossSpawn();
	Validator.Result.AnchorCount = Validator.AnchorIds.Num();
	const FString Summary = Validator.Result.ToSummary();
	if (Validator.Result.ErrorCount == 0)
	{
		UE_LOG(LogDeckSpawnAnchorValidation, Display, TEXT("[DeckSpawnAnchorValidation] Ship=%s %s"),
			*GetNameSafe(Context.GetClass()), *Summary);
	}
	else
	{
		UE_LOG(LogDeckSpawnAnchorValidation, Error, TEXT("[DeckSpawnAnchorValidation] Ship=%s %s"),
			*GetNameSafe(Context.GetClass()), *Summary);
	}
	if (bNeedsTemporaryActor) ValidationShip->Destroy();
	return Validator.Result;
}

void FDeckSpawnAnchorValidator::Error(const FString& Message)
{
	++Result.ErrorCount;
	UE_LOG(LogDeckSpawnAnchorValidation, Error, TEXT("[DeckSpawnAnchorValidation] Ship=%s %s"),
		*Ship.GetName(), *Message);
}

void FDeckSpawnAnchorValidator::CollectPoints()
{
	if (const UDeckEnemySpawnerComponent* Spawner = Ship.GetDeckEnemySpawnerComponent())
		for (const FDeckEnemySpawnSlot& Slot : Spawner->SpawnPlan) AnchorIds.Add(Slot.SpawnPointId);
	if (const UBossEncounterComponent* Encounter = Ship.GetBossEncounterComponent(); Encounter && Encounter->bEncounterEnabled)
		AnchorIds.Add(Encounter->BossSpawnPointId);
	UStaticMeshComponent* Deck = Ship.GetShipDeckMesh();
	if (!Deck || !Deck->GetStaticMesh()) Error(TEXT("Reason=MissingDeckMesh"));
	TArray<UDeckWaypointComponent*> Points;
	Ship.GetComponents<UDeckWaypointComponent>(Points);
	for (UDeckWaypointComponent* Point : Points)
	{
		if (!IsValid(Point)) continue;
		++Result.PointCount;
		const int32 Id = Point->GetWaypointId();
		if (Point->CanSpawnEnemy()) AnchorIds.Add(Id);
		if (Id < 0 || PointsById.Contains(Id))
		{
			Error(FString::Printf(TEXT("Point=%s PointId=%d Reason=%s"), *Point->GetName(), Id,
				Id < 0 ? TEXT("InvalidPointId") : TEXT("DuplicatePointId")));
		}
		else PointsById.Add(Id, Point);
		if (!Deck || !Point->IsAttachedTo(Deck))
			Error(FString::Printf(TEXT("Point=%s PointId=%d Reason=NotAttachedBelowShipDeckMesh"), *Point->GetName(), Id));
		if (Point->GetComponentTransform().ContainsNaN())
			Error(FString::Printf(TEXT("PointId=%d Reason=InvalidTransform"), Id));
	}
}

UDeckWaypointComponent* FDeckSpawnAnchorValidator::ValidateReference(
	int32 PointId, const FString& Source, bool bRequireSpawn)
{
	UDeckWaypointComponent* Point = PointsById.FindRef(PointId);
	if (!Point)
	{
		Error(FString::Printf(TEXT("%s PointId=%d Reason=UnknownPointId"), *Source, PointId));
		return nullptr;
	}
	if (bRequireSpawn && !Point->CanSpawnEnemy())
		Error(FString::Printf(TEXT("%s PointId=%d Reason=SpawnDisabled"), *Source, PointId));
	return Point;
}

void FDeckSpawnAnchorValidator::ValidateWalkArea()
{
	const UDeckWalkAreaComponent* Area = Ship.GetDeckWalkAreaComponent();
	if (!Area) { Error(TEXT("Reason=MissingWalkArea")); return; }
	Result.bCheckedRuntimeFloor = Ship.HasAuthority() && Area->IsReady();
	if (Ship.GetWorld() && Ship.GetWorld()->IsGameWorld()
		&& Ship.HasAuthority() && !Area->IsReady()) Error(TEXT("Reason=WalkAreaNotReady"));
	TArray<FDeckWalkSurfaceSettings> Surfaces = Area->Surfaces;
	if (Surfaces.IsEmpty()) Error(TEXT("Reason=MissingSurfaces"));

	TArray<UPrimitiveComponent*> Components;
	Ship.GetComponents<UPrimitiveComponent>(Components);
	TSet<FName> SurfaceIds;
	for (const FDeckWalkSurfaceSettings& Surface : Surfaces)
	{
		const FString Source = FString::Printf(TEXT("Surface=%s"), *Surface.SurfaceId.ToString());
		if (Surface.SurfaceId.IsNone() || SurfaceIds.Contains(Surface.SurfaceId))
			Error(Source + TEXT(" Reason=MissingOrDuplicateSurfaceId"));
		SurfaceIds.Add(Surface.SurfaceId);
		if (Surface.FloorComponentNames.IsEmpty()) Error(Source + TEXT(" Reason=MissingFloorSources"));
		for (FName Name : Surface.FloorComponentNames)
		{
			const UPrimitiveComponent* const* Found = Components.FindByPredicate(
				[Name](const UPrimitiveComponent* Component) { return Component->GetFName() == Name; });
			const UStaticMeshComponent* Mesh = Found ? Cast<UStaticMeshComponent>(*Found) : nullptr;
			if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->IsQueryCollisionEnabled())
				Error(Source + FString::Printf(TEXT(" Component=%s Reason=MissingOrQueryDisabledFloor"), *Name.ToString()));
		}
		if (Surface.HeightMode == EDeckWalkHeightMode::WaypointReference)
		{
			ValidateReference(Surface.HeightReferencePointId, Source + TEXT(" HeightReference"), false);
			if (!FMath::IsFinite(Surface.HeightBelowReference) || !FMath::IsFinite(Surface.HeightAboveReference)
				|| Surface.HeightBelowReference < 0.f || Surface.HeightAboveReference < 0.f
				|| Surface.HeightBelowReference + Surface.HeightAboveReference <= KINDA_SMALL_NUMBER)
				Error(Source + TEXT(" Reason=InvalidHeightOffsets"));
		}
		else if (Surface.HeightMode != EDeckWalkHeightMode::LocalRange
			|| !FMath::IsFinite(Surface.MinimumFloorZ) || !FMath::IsFinite(Surface.MaximumFloorZ)
			|| Surface.MaximumFloorZ - Surface.MinimumFloorZ <= KINDA_SMALL_NUMBER)
			Error(Source + TEXT(" Reason=InvalidHeightRange"));
		for (int32 Seed : Surface.SeedPointIds)
		{
			const UDeckWaypointComponent* Point = ValidateReference(Seed, Source + TEXT(" Seed"), false);
			if (Point && !Point->GetWalkSurfaceId().IsNone() && Point->GetWalkSurfaceId() != Surface.SurfaceId)
				Error(Source + FString::Printf(TEXT(" Seed=%d Reason=SeedSurfaceMismatch"), Seed));
		}
	}
	for (FName Name : Area->ObstacleComponentNames)
	{
		const UPrimitiveComponent* const* Found = Components.FindByPredicate(
			[Name](const UPrimitiveComponent* Component) { return Component->GetFName() == Name; });
		if (!Found || !(*Found)->IsQueryCollisionEnabled())
			Error(FString::Printf(TEXT("Component=%s Reason=MissingOrQueryDisabledObstacle"), *Name.ToString()));
	}
	for (const FDeckWalkSurfaceConnection& Link : Area->WalkingConnections)
	{
		if (!SurfaceIds.Contains(Link.FromSurface) || !SurfaceIds.Contains(Link.ToSurface) || Link.FromSurface == Link.ToSurface)
			Error(FString::Printf(TEXT("From=%s To=%s Reason=InvalidWalkingConnection"),
				*Link.FromSurface.ToString(), *Link.ToSurface.ToString()));
	}
	for (const TPair<int32, UDeckWaypointComponent*>& Pair : PointsById)
	{
		if (!AnchorIds.Contains(Pair.Key)) continue;
		const FName SurfaceId = Pair.Value->GetWalkSurfaceId();
		if ((SurfaceId.IsNone() && Surfaces.Num() > 1)
			|| (!SurfaceId.IsNone() && !SurfaceIds.Contains(SurfaceId)))
			Error(FString::Printf(TEXT("PointId=%d Surface=%s Reason=MissingOrUnknownWalkSurface"),
				Pair.Key, *SurfaceId.ToString()));
		if (Result.bCheckedRuntimeFloor)
		{
			FDeckWalkLocation Floor;
			if (!Area->ResolveWaypoint(*Pair.Value, Floor))
				Error(FString::Printf(TEXT("PointId=%d Surface=%s Reason=NoWalkableSpawnFloor"),
					Pair.Key, *SurfaceId.ToString()));
		}
	}
}

void FDeckSpawnAnchorValidator::ValidateCapsule(
	const UDeckWaypointComponent& Point, const ABaseEnemy& Enemy, const FString& Source)
{
	const UDeckWalkAreaComponent* Area = Ship.GetDeckWalkAreaComponent();
	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	if (!Area || !Capsule) return;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (Radius > FMath::Max(20.f, Area->ClearanceRadius)
		|| HalfHeight > FMath::Max(FMath::Max(20.f, Area->ClearanceRadius), Area->ClearanceHalfHeight))
		Error(Source + FString::Printf(TEXT(" PointId=%d Reason=CapsuleExceedsWalkClearance"), Point.GetWaypointId()));
	if (!Result.bCheckedRuntimeFloor) return;
	FTransform Transform;
	if (!Area->ResolveSpawnTransform(Point, HalfHeight, Transform))
	{
		Error(Source + FString::Printf(TEXT(" PointId=%d Surface=%s Reason=NoWalkableSpawnFloor"),
			Point.GetWaypointId(), *Point.GetWalkSurfaceId().ToString()));
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValidateDeckSpawnAnchor), false, &Ship);
	if (Ship.GetWorld()->OverlapBlockingTestByChannel(Transform.GetLocation(), Transform.GetRotation(), ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		Error(Source + FString::Printf(TEXT(" PointId=%d Reason=SpawnCapsuleBlockedNow"), Point.GetWaypointId()));
}

void FDeckSpawnAnchorValidator::ValidateStats(
	const FDataTableRowHandle& Override, const ABaseEnemy& Enemy, const FString& Source)
{
	const FDataTableRowHandle& Selection = Override.IsNull() ? Enemy.DefaultStatsRow : Override;
	if (Selection.IsNull()) return; // Matches the existing legacy-default balance contract.
	const UDataTable* Table = Selection.DataTable;
	const FEnemyBaseStatsRow* Row = Table && Table->GetRowStruct()
		&& Table->GetRowStruct()->IsChildOf(FEnemyBaseStatsRow::StaticStruct())
		? Table->FindRow<FEnemyBaseStatsRow>(Selection.RowName, TEXT("Spawn anchor validation"), false) : nullptr;
	if (!Row || !Row->IsValid())
	{
		Error(Source + FString::Printf(TEXT(" Row=%s Reason=InvalidStatsRow"), *Selection.RowName.ToString()));
		return;
	}
	if (!Row->CombatSettings.IsNull())
	{
		const UDataTable* CombatTable = Row->CombatSettings.DataTable;
		const FEnemyCombatBalanceRow* Combat = CombatTable && CombatTable->GetRowStruct()
			&& CombatTable->GetRowStruct()->IsChildOf(FEnemyCombatBalanceRow::StaticStruct())
			? CombatTable->FindRow<FEnemyCombatBalanceRow>(Row->CombatSettings.RowName, TEXT("Spawn anchor validation"), false) : nullptr;
		if (!Combat || !Combat->IsValid()) Error(Source + TEXT(" Reason=InvalidCombatStatsRow"));
	}
}

void FDeckSpawnAnchorValidator::ValidateSpawnPlan()
{
	const UDeckEnemySpawnerComponent* Spawner = Ship.GetDeckEnemySpawnerComponent();
	if (!Spawner) { Error(TEXT("Reason=MissingSpawner")); return; }
	if (Spawner->bEnableSpawning && Spawner->SpawnPlan.IsEmpty()) Error(TEXT("Reason=EmptyEnabledSpawnPlan"));
	if (Spawner->SpawnPlan.Num() > 32) Error(TEXT("Reason=SpawnPlanExceeds32Enemies"));
	TSet<int32> AssignedIds;
	for (int32 Index = 0; Index < Spawner->SpawnPlan.Num(); ++Index)
	{
		const FDeckEnemySpawnSlot& Slot = Spawner->SpawnPlan[Index];
		const FString Source = FString::Printf(TEXT("Slot=%d"), Index);
		AnchorIds.Add(Slot.SpawnPointId);
		if (AssignedIds.Contains(Slot.SpawnPointId)) Error(Source + TEXT(" Reason=DuplicateSpawnPointIdInPlan"));
		AssignedIds.Add(Slot.SpawnPointId);
		const UDeckWaypointComponent* Point = ValidateReference(Slot.SpawnPointId, Source, true);
		const ADeckEnemy* Enemy = Slot.EnemyClass ? Slot.EnemyClass->GetDefaultObject<ADeckEnemy>() : nullptr;
		if (!Enemy || Slot.EnemyClass->HasAnyClassFlags(CLASS_Abstract)) Error(Source + TEXT(" Reason=MissingOrAbstractEnemyClass"));
		else
		{
			ValidateStats(Slot.StatsRow, *Enemy, Source);
			if (Point) ValidateCapsule(*Point, *Enemy, Source);
		}
	}
}

void FDeckSpawnAnchorValidator::ValidateBossSpawn()
{
	const UBossEncounterComponent* Encounter = Ship.GetBossEncounterComponent();
	if (!Encounter || !Encounter->bEncounterEnabled) return;
	AnchorIds.Add(Encounter->BossSpawnPointId);
	// Boss spawning historically uses its exact authored point, independently of CanSpawnEnemy.
	const UDeckWaypointComponent* Point = ValidateReference(Encounter->BossSpawnPointId, TEXT("BossSpawn"), false);
	const AShipBossEnemy* Boss = Encounter->BossClass ? Encounter->BossClass->GetDefaultObject<AShipBossEnemy>() : nullptr;
	if (!Boss || Encounter->BossClass->HasAnyClassFlags(CLASS_Abstract)) Error(TEXT("BossSpawn Reason=MissingOrAbstractBossClass"));
	else
	{
		ValidateStats(Encounter->BossStatsRow, *Boss, TEXT("BossSpawn"));
		if (Point) ValidateCapsule(*Point, *Boss, TEXT("BossSpawn"));
	}
}
#endif
