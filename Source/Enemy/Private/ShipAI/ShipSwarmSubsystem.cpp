// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipAvoidanceSettings.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Ship.h"
#include "ShipAttributeSet.h"

namespace EnemyShipAvoidance
{
	struct FPlanarForecast
	{
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Velocity = FVector2D::ZeroVector;
		float YawRadians = 0.0f;
		float YawRateRadians = 0.0f;
		float HalfLength = 1800.0f;
		float HalfWidth = 735.0f;
		float ForwardAcceleration = 0.0f;
		float YawAcceleration = 0.0f;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
	};

	FPlanarForecast MakeForecast(const AEnemyShip& Ship)
	{
		FPlanarForecast Result;
		const FVector Location = Ship.GetActorLocation();
		Result.Position = FVector2D(Location.X, Location.Y);
		const FVector Velocity = Ship.BuoyancyRoot
			? Ship.BuoyancyRoot->GetPhysicsLinearVelocity()
			: Ship.GetVelocity();
		Result.Velocity = FVector2D(Velocity.X, Velocity.Y);
		Result.YawRadians = FMath::DegreesToRadians(Ship.GetActorRotation().Yaw);

		float MassKg = 10000.0f;
		if (Ship.BuoyancyRoot)
		{
			const FVector Scale = Ship.BuoyancyRoot->GetComponentScale().GetAbs();
			if (const UStaticMesh* Mesh = Ship.BuoyancyRoot->GetStaticMesh())
			{
				const FVector Extent = Mesh->GetBoundingBox().GetExtent() * Scale;
				Result.HalfLength = FMath::Max(1.0f, Extent.X);
				Result.HalfWidth = FMath::Max(1.0f, Extent.Y);
			}
			Result.YawRateRadians = Ship.BuoyancyRoot->GetPhysicsAngularVelocityInRadians().Z;
			if (Ship.BuoyancyRoot->IsSimulatingPhysics())
			{
				MassKg = FMath::Max(1.0f, Ship.BuoyancyRoot->GetMass());
			}
			Result.LinearDamping = FMath::Max(0.0f, Ship.BuoyancyRoot->GetLinearDamping());
			Result.AngularDamping = FMath::Max(0.0f, Ship.BuoyancyRoot->GetAngularDamping());
		}

		const UShipAttributeSet* Attributes = Ship.GetShipAttributeSet();
		const float PropulsionMultiplier = Attributes ? Attributes->GetForwardPropulsionMultiplier() : 1.0f;
		const float TurnMultiplier = Attributes ? Attributes->GetTurnTorqueMultiplier() : 1.0f;
		Result.ForwardAcceleration = Ship.GetForwardForceMagnitude()
			* Ship.GetCurrentMoveInput()
			* PropulsionMultiplier
			* Ship.GetCurrentAIPropulsionScale()
			/ MassKg;

		// Rectangle inertia is a conservative, stable approximation of the authored hull body.
		const float YawInertia = MassKg / 3.0f
			* (FMath::Square(Result.HalfLength) + FMath::Square(Result.HalfWidth));
		Result.YawAcceleration = Ship.GetTurnTorqueMagnitude()
			* Ship.GetCurrentTurnInput()
			* TurnMultiplier
			* Ship.GetCurrentAITurnScale()
			/ FMath::Max(1.0f, YawInertia);
		return Result;
	}

	FPlanarForecast MakeForecast(const AEnemyShipObstacle& Obstacle)
	{
		FPlanarForecast Result;
		const FVector Location = Obstacle.GetActorLocation();
		const FVector Velocity = Obstacle.GetVelocity();
		const FVector Extent = Obstacle.GetAvoidanceHalfExtent();
		Result.Position = FVector2D(Location.X, Location.Y);
		Result.Velocity = FVector2D(Velocity.X, Velocity.Y);
		Result.YawRadians = FMath::DegreesToRadians(Obstacle.GetActorRotation().Yaw);
		Result.HalfLength = FMath::Max(1.0f, Extent.X);
		Result.HalfWidth = FMath::Max(1.0f, Extent.Y);
		return Result;
	}

	void Advance(FPlanarForecast& State, float DeltaTime)
	{
		const FVector2D Forward(FMath::Cos(State.YawRadians), FMath::Sin(State.YawRadians));
		State.Velocity += Forward * State.ForwardAcceleration * DeltaTime;
		State.Velocity *= FMath::Exp(-State.LinearDamping * DeltaTime);
		State.YawRateRadians += State.YawAcceleration * DeltaTime;
		State.YawRateRadians *= FMath::Exp(-State.AngularDamping * DeltaTime);
		State.YawRadians += State.YawRateRadians * DeltaTime;
		State.Position += State.Velocity * DeltaTime;
	}

	bool Overlaps(const FPlanarForecast& A, const FPlanarForecast& B, float Margin)
	{
		const FVector2D AForward(FMath::Cos(A.YawRadians), FMath::Sin(A.YawRadians));
		const FVector2D ARight(-AForward.Y, AForward.X);
		const FVector2D BForward(FMath::Cos(B.YawRadians), FMath::Sin(B.YawRadians));
		const FVector2D BRight(-BForward.Y, BForward.X);
		const FVector2D Delta = B.Position - A.Position;
		const FVector2D Axes[] = {AForward, ARight, BForward, BRight};
		for (const FVector2D& Axis : Axes)
		{
			const float CenterDistance = FMath::Abs(FVector2D::DotProduct(Delta, Axis));
			const float ARadius = A.HalfLength * FMath::Abs(FVector2D::DotProduct(AForward, Axis))
				+ A.HalfWidth * FMath::Abs(FVector2D::DotProduct(ARight, Axis));
			const float BRadius = B.HalfLength * FMath::Abs(FVector2D::DotProduct(BForward, Axis))
				+ B.HalfWidth * FMath::Abs(FVector2D::DotProduct(BRight, Axis));
			if (CenterDistance > ARadius + BRadius + Margin)
			{
				return false;
			}
		}
		return true;
	}

	bool IsEligiblePeer(const AEnemyShip& Ship, const AEnemyShip& Other)
	{
		const UEnemyShipNavigationComponent* Navigation = Ship.GetNavigationComponent();
		const UEnemyShipNavigationComponent* OtherNavigation = Other.GetNavigationComponent();
		const bool bShipInCombatNavigation = Navigation
			&& (Navigation->GetCurrentState() == ENavalCombatState::Approach
				|| Navigation->GetCurrentState() == ENavalCombatState::Orbit);
		const bool bOtherInCombatNavigation = OtherNavigation
			&& (OtherNavigation->GetCurrentState() == ENavalCombatState::Approach
				|| OtherNavigation->GetCurrentState() == ENavalCombatState::Orbit);
		if (&Ship == &Other
			|| Ship.SquadID != Other.SquadID
			|| !Navigation || !OtherNavigation
			|| !bShipInCombatNavigation || !bOtherInCombatNavigation
			|| !Navigation->IsNavigationEnabled()
			|| Ship.IsDeathHandled() || Other.IsDeathHandled())
		{
			return false;
		}

		return OtherNavigation->IsNavigationEnabled()
			&& Ship.SquadID == Other.SquadID
			&& Navigation->GetTargetShip()
			&& Navigation->GetTargetShip() == OtherNavigation->GetTargetShip();
	}

	bool MustYield(const AEnemyShip& Ship, const AEnemyShip& Other)
	{
		const UEnemyShipNavigationComponent* OtherNavigation = Other.GetNavigationComponent();
		if (OtherNavigation && OtherNavigation->HasActiveOverride())
		{
			return true;
		}

		// A total ordering means exactly one ordinary ship yields and prevents reciprocal deadlock.
		return Other.GetFName().LexicalLess(Ship.GetFName());
	}
}

void UShipSwarmSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() != NM_Client)
	{
		InWorld.GetTimerManager().SetTimer(
			DistanceOptimizationTimerHandle,
			this,
			&UShipSwarmSubsystem::EvaluateDistanceOptimization,
			0.5f,
			true,
			0.5f);
	}
}

void UShipSwarmSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DistanceOptimizationTimerHandle);
	}
	SquadMap.Reset();
	Super::Deinitialize();
}

void UShipSwarmSubsystem::EvaluateDistanceOptimization()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	TArray<FVector> PlayerShipLocations;
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		const AShip* Ship = *It;
		if (IsValid(Ship) && !Ship->IsEnemyShipForEffects()
			&& Ship->ActorHasTag(TEXT("Player")) && !Ship->ActorHasTag(TEXT("Enemy")))
		{
			PlayerShipLocations.Add(Ship->GetActorLocation());
		}
	}

	TSet<AEnemyShip*> EvaluatedShips;
	for (TPair<FName, TArray<TWeakObjectPtr<AEnemyShip>>>& SquadPair : SquadMap)
	{
		for (int32 Index = SquadPair.Value.Num() - 1; Index >= 0; --Index)
		{
			AEnemyShip* EnemyShip = SquadPair.Value[Index].Get();
			if (!IsValid(EnemyShip))
			{
				SquadPair.Value.RemoveAtSwap(Index);
				continue;
			}
			if (!EnemyShip->IsDistanceOptimizationEnabled() || EvaluatedShips.Contains(EnemyShip))
			{
				continue;
			}
			EvaluatedShips.Add(EnemyShip);

			const float RangeSquared = FMath::Square(FMath::Max(0.0f, EnemyShip->GetDistanceOptimizationRange()));
			const bool bPlayerInRange = PlayerShipLocations.ContainsByPredicate(
				[EnemyShip, RangeSquared](const FVector& PlayerLocation)
				{
					return FVector::DistSquared2D(EnemyShip->GetActorLocation(), PlayerLocation)
						<= RangeSquared;
				});

			if (EnemyShip->IsDistanceOptimizationDormant())
			{
				if (bPlayerInRange)
				{
					EnemyShip->SetDistanceOptimizationDormant(false);
				}
			}
			else if (!bPlayerInRange && EnemyShip->CanEnterDistanceOptimizationDormancy())
			{
				EnemyShip->SetDistanceOptimizationDormant(true);
			}
		}
	}
}

void UShipSwarmSubsystem::RegisterShip(AEnemyShip* Ship)
{
	if (!Ship) return;

	FName SquadID = Ship->SquadID;
	TArray<TWeakObjectPtr<AEnemyShip>>& SquadArray = SquadMap.FindOrAdd(SquadID);

	// 중복 등록 방지
	TWeakObjectPtr<AEnemyShip> ShipWeakPtr(Ship);
	if (!SquadArray.Contains(ShipWeakPtr))
	{
		SquadArray.Add(ShipWeakPtr);
		UE_LOG(LogTemp, Log, TEXT("UShipSwarmSubsystem::RegisterShip - Registered [%s] to Squad [%s]. Total members: %d"), 
			*Ship->GetName(), *SquadID.ToString(), SquadArray.Num());
		RecalculateSquadOrbitDistances(SquadID);
	}
}

void UShipSwarmSubsystem::UnregisterShip(AEnemyShip* Ship)
{
	if (!Ship) return;

	FName SquadID = Ship->SquadID;
	if (TArray<TWeakObjectPtr<AEnemyShip>>* SquadArray = SquadMap.Find(SquadID))
	{
		TWeakObjectPtr<AEnemyShip> ShipWeakPtr(Ship);
		int32 RemovedCount = SquadArray->Remove(ShipWeakPtr);
		if (RemovedCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("UShipSwarmSubsystem::UnregisterShip - Unregistered [%s] from Squad [%s]. Remaining members: %d"), 
				*Ship->GetName(), *SquadID.ToString(), SquadArray->Num());
		}

		// 군집이 비어있으면 맵에서 정리
		if (SquadArray->Num() == 0)
		{
			SquadMap.Remove(SquadID);
		}
		else if (RemovedCount > 0)
		{
			RecalculateSquadOrbitDistances(SquadID);
		}
	}
}

TArray<AEnemyShip*> UShipSwarmSubsystem::GetSquadMembers(FName SquadID)
{
	TArray<AEnemyShip*> ValidMembers;
	
	if (TArray<TWeakObjectPtr<AEnemyShip>>* SquadArray = SquadMap.Find(SquadID))
	{
		// 역순으로 탐색하여 파괴된 객체(Null) 정리와 동시에 유효한 멤버 수집
		for (int32 i = SquadArray->Num() - 1; i >= 0; --i)
		{
			if (AEnemyShip* Member = (*SquadArray)[i].Get())
			{
				ValidMembers.Add(Member);
			}
			else
			{
				// 파괴되어 메모리에서 사라진 배 자동 정리 (TWeakObjectPtr의 강점)
				SquadArray->RemoveAtSwap(i);
			}
		}
	}

	return ValidMembers;
}

void UShipSwarmSubsystem::RecalculateSquadOrbitDistances(FName SquadID)
{
	TArray<AEnemyShip*> Members = GetSquadMembers(SquadID);
	Members.RemoveAll([](const AEnemyShip* Member)
	{
		return !IsValid(Member) || !IsValid(Member->EnemyShipArchetype) || !Member->GetNavigationComponent();
	});
	if (Members.IsEmpty())
	{
		return;
	}

	Members.Sort([](const AEnemyShip& Left, const AEnemyShip& Right)
	{
		return Left.GetFName().LexicalLess(Right.GetFName());
	});

	float IdealDistanceSum = 0.0f;
	float SpacingSum = 0.0f;
	for (const AEnemyShip* Member : Members)
	{
		IdealDistanceSum += Member->EnemyShipArchetype->NavigationProfile.IdealDistance;
		SpacingSum += FMath::Max(0.0f, Member->EnemyShipArchetype->OrbitDistanceSpacing);
	}

	const float AverageIdealDistance = IdealDistanceSum / Members.Num();
	const float AverageSpacing = SpacingSum / Members.Num();
	const float CenterIndex = (Members.Num() - 1) * 0.5f;
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		const float AssignedDistance = AverageIdealDistance + (Index - CenterIndex) * AverageSpacing;
		Members[Index]->SetSquadAssignedIdealDistance(FMath::Max(1.0f, AssignedDistance));
	}
}

FEnemyShipAvoidanceDecision UShipSwarmSubsystem::EvaluateAvoidance(AEnemyShip* Ship)
{
	FEnemyShipAvoidanceDecision Decision;
	const UEnemyShipNavigationComponent* Navigation = IsValid(Ship)
		? Ship->GetNavigationComponent()
		: nullptr;
	if (!Navigation
		|| (Navigation->GetCurrentState() != ENavalCombatState::Approach
			&& Navigation->GetCurrentState() != ENavalCombatState::Orbit))
	{
		return Decision;
	}

	const UEnemyShipAvoidanceSettings* Settings = GetDefault<UEnemyShipAvoidanceSettings>();
	TArray<AEnemyShip*> Members = GetSquadMembers(Ship->SquadID);
	Members.RemoveAll([Ship](const AEnemyShip* Other)
	{
		return !IsValid(Other) || !EnemyShipAvoidance::IsEligiblePeer(*Ship, *Other);
	});
	Members.Sort([Ship](const AEnemyShip& Left, const AEnemyShip& Right)
	{
		return FVector::DistSquared2D(Ship->GetActorLocation(), Left.GetActorLocation())
			< FVector::DistSquared2D(Ship->GetActorLocation(), Right.GetActorLocation());
	});
	Members.SetNum(FMath::Min(Members.Num(), FMath::Max(0, Settings->MaximumEvaluatedShips - 1)));

	for (AEnemyShip* Other : Members)
	{
		if (!EnemyShipAvoidance::MustYield(*Ship, *Other))
		{
			continue;
		}

		EnemyShipAvoidance::FPlanarForecast SelfForecast = EnemyShipAvoidance::MakeForecast(*Ship);
		EnemyShipAvoidance::FPlanarForecast OtherForecast = EnemyShipAvoidance::MakeForecast(*Other);
		const float Step = FMath::Max(0.05f, Settings->PredictionStep);
		const float Horizon = FMath::Max(Step, Settings->PredictionHorizon);
		for (float Time = 0.0f; Time <= Horizon + UE_SMALL_NUMBER; Time += Step)
		{
			const float Margin = FMath::Max(0.0f, Settings->HullSafetyMargin)
				+ FMath::Max(0.0f, Settings->UncertaintyGrowthPerSecond) * Time;
			if (EnemyShipAvoidance::Overlaps(SelfForecast, OtherForecast, Margin))
			{
				if (Time < Decision.EarliestCollisionTime)
				{
					Decision.bShouldYield = true;
					Decision.EarliestCollisionTime = Time;
					Decision.ThreatActor = Other;
				}
				break;
			}
			EnemyShipAvoidance::Advance(SelfForecast, Step);
			EnemyShipAvoidance::Advance(OtherForecast, Step);
		}
	}

	TArray<AEnemyShipObstacle*> Obstacles;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AEnemyShipObstacle> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Obstacles.Add(*It);
			}
		}
	}
	Obstacles.Sort([Ship](const AEnemyShipObstacle& Left, const AEnemyShipObstacle& Right)
	{
		return FVector::DistSquared2D(Ship->GetActorLocation(), Left.GetActorLocation())
			< FVector::DistSquared2D(Ship->GetActorLocation(), Right.GetActorLocation());
	});
	Obstacles.SetNum(FMath::Min(
		Obstacles.Num(),
		FMath::Max(0, Settings->MaximumEvaluatedObstacles)));

	for (AEnemyShipObstacle* Obstacle : Obstacles)
	{
		EnemyShipAvoidance::FPlanarForecast SelfForecast = EnemyShipAvoidance::MakeForecast(*Ship);
		EnemyShipAvoidance::FPlanarForecast ObstacleForecast = EnemyShipAvoidance::MakeForecast(*Obstacle);
		const float Step = FMath::Max(0.05f, Settings->PredictionStep);
		const float Horizon = FMath::Max(Step, Settings->PredictionHorizon);
		for (float Time = 0.0f; Time <= Horizon + UE_SMALL_NUMBER; Time += Step)
		{
			const float Margin = FMath::Max(0.0f, Settings->HullSafetyMargin)
				+ FMath::Max(0.0f, Settings->UncertaintyGrowthPerSecond) * Time;
			if (EnemyShipAvoidance::Overlaps(SelfForecast, ObstacleForecast, Margin))
			{
				if (Time < Decision.EarliestCollisionTime)
				{
					Decision.bShouldYield = true;
					Decision.EarliestCollisionTime = Time;
					Decision.ThreatActor = Obstacle;
					Decision.bOverrideTurnInput = true;
					const FVector ToObstacle = Obstacle->GetActorLocation() - Ship->GetActorLocation();
					const float ObstacleRight = FVector::DotProduct(Ship->GetActorRightVector(), ToObstacle);
					Decision.TurnInput = FMath::IsNearlyZero(ObstacleRight)
						? 1.0f
						: (ObstacleRight > 0.0f ? -1.0f : 1.0f);
				}
				break;
			}
			EnemyShipAvoidance::Advance(SelfForecast, Step);
			EnemyShipAvoidance::Advance(ObstacleForecast, Step);
		}
	}
	return Decision;
}
