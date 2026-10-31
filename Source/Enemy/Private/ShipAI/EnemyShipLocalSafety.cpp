#include "ShipAI/EnemyShipLocalSafety.h"

#include "Ship.h"
#include "ShipAttributeSet.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAvoidanceSettings.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

namespace EnemyShipLocalSafety
{
	float Value(float Input, float Fallback, float Minimum, float Maximum)
	{
		return FMath::Clamp(FMath::IsFinite(Input) ? Input : Fallback, Minimum, Maximum);
	}

	FVector FHull::Center() const
	{
		return Position + FRotator(0.0f, Yaw, 0.0f).RotateVector(CenterOffset);
	}

	void FHull::Advance(float Dt)
	{
		if (bHold) { Velocity.X = Velocity.Y = 0.0f; YawRate = 0.0f; return; }
		Velocity += FRotator(0.0f, Yaw, 0.0f).Vector() * Acceleration * Dt;
		Velocity *= FMath::Exp(-Damping * Dt);
		if (MaximumSpeed > 0.0f) Velocity = Velocity.GetClampedToMaxSize(MaximumSpeed);
		YawRate = (YawRate + YawAcceleration * Dt) * FMath::Exp(-AngularDamping * Dt);
		if (MaximumYawRate > 0.0f) YawRate = FMath::Clamp(YawRate, -MaximumYawRate, MaximumYawRate);
		Yaw += YawRate * Dt;
		Position += Velocity * Dt;
	}

	static bool MakeHull(AEnemyShip& Ship, const FEnemyShipNavigationOverrideRequest& Control, FHull& Out)
	{
		UStaticMeshComponent* Root = Ship.BuoyancyRoot;
		if (!Root || !Root->GetStaticMesh() || !Root->IsRegistered()) return false;
		FBox Local = Root->GetStaticMesh()->GetBoundingBox();
		if (UBodySetup* Body = Root->GetBodySetup())
		{
			const FBox PhysicsBox = Body->AggGeom.CalcAABB(FTransform::Identity);
			if (PhysicsBox.IsValid) Local += PhysicsBox;
		}
		const FVector Scale = Root->GetComponentScale().GetAbs();
		Out.Actor = &Ship;
		Out.Position = Root->GetComponentLocation();
		Out.Yaw = Root->GetComponentRotation().Yaw;
		Out.CenterOffset = Local.GetCenter() * Scale;
		Out.Extent = Local.GetExtent() * Scale;
		// Include pitch/roll projection without treating a rotated world AABB as local hull size.
		const FQuat Tilt = FRotator(0.0f, -Out.Yaw, 0.0f).Quaternion() * Root->GetComponentQuat();
		Out.CenterOffset = Tilt.RotateVector(Out.CenterOffset);
		const FVector X = Tilt.RotateVector(FVector(Out.Extent.X, 0, 0)).GetAbs();
		const FVector Y = Tilt.RotateVector(FVector(0, Out.Extent.Y, 0)).GetAbs();
		const FVector Z = Tilt.RotateVector(FVector(0, 0, Out.Extent.Z)).GetAbs();
		Out.Extent = X + Y + Z;
		Out.Velocity = Root->GetPhysicsLinearVelocity();
		Out.Velocity.Z = 0.0f;
		Out.YawRate = Root->GetPhysicsAngularVelocityInDegrees().Z;
		const float Mass = Root->IsSimulatingPhysics() ? FMath::Max(1.0f, Root->GetMass()) : 10000.0f;
		const UShipAttributeSet* Attributes = Ship.GetShipAttributeSet();
		Out.Acceleration = Ship.GetForwardForceMagnitude() * Control.MoveInput * Control.PropulsionMultiplier
			* (Attributes ? Attributes->GetForwardPropulsionMultiplier() : 1.0f) / Mass;
		const float Inertia = Mass / 3.0f * (FMath::Square(Out.Extent.X) + FMath::Square(Out.Extent.Y));
		Out.YawAcceleration = FMath::RadiansToDegrees(Ship.GetTurnTorqueMagnitude() * Control.TurnInput
			* Control.TurnMultiplier * (Attributes ? Attributes->GetTurnTorqueMultiplier() : 1.0f) / FMath::Max(1.0f, Inertia));
		Out.Damping = FMath::Max(0.0f, Root->GetLinearDamping());
		Out.AngularDamping = FMath::Max(0.0f, Root->GetAngularDamping());
		return !Out.Position.ContainsNaN() && !Out.Velocity.ContainsNaN() && !Out.Extent.ContainsNaN()
			&& Out.Extent.GetMin() > 0.0f && FMath::IsFinite(Out.Acceleration) && FMath::IsFinite(Out.YawAcceleration)
			&& FMath::IsFinite(Out.YawRate);
	}

	static float Separation(const FHull& A, const FHull& B)
	{
		const FVector AX = FRotator(0, A.Yaw, 0).Vector();
		const FVector AY(-AX.Y, AX.X, 0);
		const FVector BX = FRotator(0, B.Yaw, 0).Vector();
		const FVector BY(-BX.Y, BX.X, 0);
		const FVector Delta = B.Center() - A.Center();
		float Gap = -TNumericLimits<float>::Max();
		for (const FVector& Axis : { AX, AY, BX, BY })
		{
			const float Radius = A.Extent.X * FMath::Abs(FVector::DotProduct(AX, Axis))
				+ A.Extent.Y * FMath::Abs(FVector::DotProduct(AY, Axis))
				+ B.Extent.X * FMath::Abs(FVector::DotProduct(BX, Axis))
				+ B.Extent.Y * FMath::Abs(FVector::DotProduct(BY, Axis));
			Gap = FMath::Max(Gap, static_cast<float>(FMath::Abs(FVector::DotProduct(Delta, Axis)) - Radius));
		}
		return Gap;
	}

	static bool IsTerrain(const FHitResult& Hit, const AEnemyShip& Ship)
	{
		AActor* Actor = Hit.GetActor();
		UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Actor || !Component || Actor == &Ship || Actor->IsAttachedTo(&Ship)
			|| Actor->IsA<AWaterBody>() || Component->IsA<UWaterBodyComponent>()
			|| Component->GetCollisionProfileName() == TEXT("Water") || !Component->IsPhysicsCollisionEnabled()
			|| Component->GetCollisionResponseToChannel(Ship.BuoyancyRoot->GetCollisionObjectType()) != ECR_Block) return false;
		if (Component->IsA<UStaticMeshComponent>()) return true;
		for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
			if (Class->GetName().StartsWith(TEXT("Landscape"))) return true;
		return false;
	}

	static FResult TerrainSegment(AEnemyShip& Ship, const FHull& Start, const FHull& End,
		float Margin, const TArray<TWeakObjectPtr<AActor>>& Water)
	{
		UWorld* World = Ship.GetWorld();
		if (!World) return { false, EThreat::Invalid, nullptr, TEXT("WorldUnavailable") };
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShipLocalTerrain), false, &Ship);
		for (const auto& Weak : Water) if (Weak.IsValid()) Params.AddIgnoredActor(Weak.Get());
		FCollisionObjectQueryParams Objects(ECC_TO_BITFIELD(ECC_WorldStatic));
		const float Angle = FMath::DegreesToRadians(FMath::Abs(End.Yaw - Start.Yaw));
		const float RotationMargin = (Start.Extent.Size2D() + Start.CenterOffset.Size2D()) * FMath::Min(Angle, 2.0f);
		const FVector Extent = Start.Extent + FVector(Margin + RotationMargin, Margin + RotationMargin, 0);
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, Start.Center(), End.Center(), FRotator(0, Start.Yaw, 0).Quaternion(),
			Objects, FCollisionShape::MakeBox(Extent), Params);
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit || !IsTerrain(Hit, Ship)) continue;
			if (Hit.bStartPenetrating && FMath::IsFinite(Hit.PenetrationDepth) && !Hit.Normal.IsNearlyZero())
			{
				const float Separating = FVector::DotProduct(End.Center() - Start.Center(), Hit.Normal);
				if (Separating > RotationMargin + UE_KINDA_SMALL_NUMBER) continue;
			}
			FResult Result{ false, EThreat::Terrain, Hit.GetActor(), TEXT("TerrainSweepBlocked") };
			Result.HitDistance = Hit.Distance;
			Result.PenetrationDepth = Hit.PenetrationDepth;
			Result.bStartPenetrating = Hit.bStartPenetrating;
			Result.ComponentName = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
			return Result;
		}
		return {};
	}

	FQuery::FQuery(AEnemyShip& Ship, const TArray<TWeakObjectPtr<AActor>>& WaterBodies)
		: Owner(Ship), Settings(*GetDefault<UEnemyShipAvoidanceSettings>()), IgnoredWater(WaterBodies)
	{
		bValid = MakeHull(Ship, {}, Hull);
		if (!bValid || !Ship.GetWorld()) return;
		TArray<TWeakObjectPtr<AEnemyShip>> Nearby;
		if (const UShipSwarmSubsystem* Swarm = Ship.GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
			Swarm->GatherSafetyPeers(&Ship, Nearby);
		else { bValid = false; return; }
		for (const auto& Weak : Nearby)
		{
			AEnemyShip* Other = Weak.Get();
			if (!Other) continue;
			FEnemyShipNavigationOverrideRequest Control;
			Control.MoveInput = Other->GetCurrentMoveInput();
			Control.TurnInput = Other->GetCurrentTurnInput();
			Control.PropulsionMultiplier = Other->GetCurrentAIPropulsionScale();
			Control.TurnMultiplier = Other->GetCurrentAITurnScale();
			FHull Peer;
			if (!MakeHull(*Other, Control, Peer)) { bValid = false; return; }
			const FShipAISafetyControl& Safety = Other->GetAISafetyControl();
			Peer.bHold = Safety.Mode == EShipAISafetyMode::Hold;
			if (Safety.Mode == EShipAISafetyMode::Escape)
			{
				Peer.MaximumSpeed = Safety.MaximumSpeed;
				Peer.MaximumYawRate = Safety.MaximumYawRate;
			}
			Peers.Add(Peer);
		}
		if (Ship.GetNavigationComponent() && Ship.GetNavigationComponent()->GetCurrentState() != ENavalCombatState::Return)
		{
			for (TActorIterator<AEnemyShipObstacle> It(Ship.GetWorld()); It; ++It)
			{
				if (!It->GetActorEnableCollision()) continue;
				FHull Obstacle;
				Obstacle.Actor = *It;
				Obstacle.Position = It->GetActorLocation();
				Obstacle.Velocity = It->GetVelocity();
				Obstacle.Velocity.Z = 0;
				Obstacle.Yaw = It->GetActorRotation().Yaw;
				Obstacle.Extent = It->GetAvoidanceHalfExtent();
				Peers.Add(Obstacle);
			}
		}
	}

	FResult FQuery::CheckPath(const TArray<FHull>& Path, float Duration, float Margin) const
	{
		if (!bValid || Path.Num() < 2) return { false, EThreat::Invalid, nullptr, TEXT("InvalidPath") };
		FResult Result;
		Result.Margin = Margin;
		Result.Segments = Path.Num() - 1;
		Result.Peers = Peers.Num();
		const float Dt = Duration / (Path.Num() - 1);
		float OwnReach = 0.0f;
		for (const FHull& Pose : Path)
			OwnReach = FMath::Max(OwnReach, static_cast<float>((Pose.Center() - Path[0].Position).Size2D()));
		for (const FHull& InitialPeer : Peers)
		{
			const float Reach = Path[0].Extent.Size2D() + InitialPeer.Extent.Size2D() + Margin
				+ OwnReach + InitialPeer.CenterOffset.Size2D()
				+ InitialPeer.Velocity.Size2D() * Duration + FMath::Abs(InitialPeer.Acceleration) * Duration * Duration;
			if (FVector::DistSquared2D(Path[0].Position, InitialPeer.Position) > FMath::Square(Reach)) continue;
			++Result.TestedPeers;
			FHull Peer = InitialPeer;
			const float InitialGap = Separation(Path[0], Peer);
			float PreviousGap = InitialGap;
			for (int32 Index = 1; Index < Path.Num(); ++Index)
			{
				const FHull PreviousPeer = Peer;
				Peer.Advance(Dt);
				const float Gap = Separation(Path[Index], Peer);
				const float SelfRotation = FMath::DegreesToRadians(FMath::Abs(Path[Index].Yaw - Path[Index - 1].Yaw)) * Path[Index].Extent.Size2D();
				const float PeerRotation = FMath::DegreesToRadians(FMath::Abs(Peer.Yaw - PreviousPeer.Yaw)) * Peer.Extent.Size2D();
				const float RelativeTravel = ((Path[Index].Center() - Path[Index - 1].Center()) - (Peer.Center() - PreviousPeer.Center())).Size2D();
				const float Envelope = SelfRotation + PeerRotation + RelativeTravel;
				const bool bSeparatingExisting = InitialGap < Margin && Gap >= PreviousGap - 0.1f
					&& (InitialGap <= 0.0f || Gap > 0.0f);
				const bool bEnvelopeBlocked = !bSeparatingExisting && FMath::Min(PreviousGap, Gap) - Envelope < Margin;
				const bool bNotSeparating = InitialGap < Margin && Index == Path.Num() - 1 && Gap <= InitialGap + 0.1f;
				if (bEnvelopeBlocked || bNotSeparating)
				{
					Result.bSafe = false;
					Result.Threat = EThreat::Ship;
					Result.Actor = Peer.Actor;
					Result.Reason = bEnvelopeBlocked ? TEXT("ShipEnvelopeBlocked") : TEXT("ExistingOverlapNotSeparating");
					Result.InitialGap = InitialGap;
					Result.PreviousGap = PreviousGap;
					Result.Gap = Gap;
					Result.Envelope = Envelope;
					Result.Segment = Index;
					Result.PredictionTime = Dt * Index;
					return Result;
				}
				PreviousGap = Gap;
			}
		}
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			const FResult Terrain = TerrainSegment(Owner, Path[Index - 1], Path[Index], Margin, IgnoredWater);
			++Result.TerrainSweeps;
			if (!Terrain.bSafe)
			{
				FResult Blocked = Terrain;
				Blocked.Margin = Margin;
				Blocked.Segment = Index;
				Blocked.Segments = Result.Segments;
				Blocked.Peers = Result.Peers;
				Blocked.TestedPeers = Result.TestedPeers;
				Blocked.TerrainSweeps = Result.TerrainSweeps;
				Blocked.PredictionTime = Dt * Index;
				return Blocked;
			}
		}
		return Result;
	}

	FResult FQuery::Check(const FEnemyShipNavigationOverrideRequest& Control, bool bEscape, float ExtraMargin) const
	{
		FHull Current;
		if (!bValid || !MakeHull(Owner, Control, Current)) return { false, EThreat::Invalid, nullptr, TEXT("HullOrPeerUnavailable") };
		if (bEscape)
		{
			Current.MaximumSpeed = Value(Settings.EscapeMaximumSpeed, 300, 1);
			Current.MaximumYawRate = Value(Settings.EscapeMaximumYawRate, 8, 0.1f, 180);
		}
		const float Duration = Value(Settings.SafetyPredictionTime, 1.5f, 0.1f, 5)
			+ Value(Settings.SafetyLatencyAllowance, 0.2f, 0, 2);
		const float Step = Value(Settings.SafetyPredictionStep, 0.25f, 0.02f, 0.5f);
		const float YawStep = Value(Settings.SafetyMaximumYawStep, 5, 0.5f, 15);
		const FHull Initial = Current;
		const int32 MaximumSegments = FMath::Clamp(Settings.SafetyMaximumSegments, 1, 64);
		int32 Count = FMath::CeilToInt(Duration / Step);
		auto RejectPrediction = [&](const TCHAR* Reason)
		{
			FResult Result{ false, EThreat::Invalid, nullptr, Reason };
			Result.Segments = Count;
			Result.PredictionTime = Duration;
			Result.Peers = Peers.Num();
			Result.ForecastAcceleration = Initial.Acceleration;
			Result.ForecastYawRate = Initial.YawRate;
			Result.ForecastYawAcceleration = Initial.YawAcceleration;
			Result.LinearDamping = Initial.Damping;
			Result.AngularDamping = Initial.AngularDamping;
			Result.HullExtent = Initial.Extent;
			return Result;
		};
		if (Count > MaximumSegments) return RejectPrediction(TEXT("SegmentBudgetExceeded"));
		TArray<FHull, TInlineAllocator<33>> Scratch;
		for (;;)
		{
			Current = Initial;
			Scratch.Reset();
			Scratch.Add(Current);
			float MaximumSegmentYaw = 0.0f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float PreviousYaw = Current.Yaw;
				Current.Advance(Duration / Count);
				if (!FMath::IsFinite(Current.Yaw) || !FMath::IsFinite(Current.YawRate)
					|| Current.Position.ContainsNaN() || Current.Velocity.ContainsNaN())
					return RejectPrediction(TEXT("InvalidForecast"));
				MaximumSegmentYaw = FMath::Max(MaximumSegmentYaw, FMath::Abs(Current.Yaw - PreviousYaw));
				Scratch.Add(Current);
			}
			// Validate the same damped, speed-limited trajectory that collision checks consume.
			if (MaximumSegmentYaw <= YawStep) break;
			if (Count == MaximumSegments) return RejectPrediction(TEXT("SegmentBudgetExceeded"));
			const float RefinementScale = FMath::Min(MaximumSegmentYaw / YawStep, static_cast<float>(MaximumSegments));
			Count = FMath::Min(MaximumSegments, FMath::Max(Count + 1, FMath::CeilToInt(Count * RefinementScale)));
		}
		// A stopped hull must still reserve a non-zero departure corridor.
		const float MinimumDistance = Value(Settings.SafetyMinimumProbeDistance, 300);
		const FVector Travel = Scratch.Last().Position - Scratch[0].Position;
		if (FMath::Abs(Control.MoveInput) > UE_KINDA_SMALL_NUMBER && Travel.Size2D() < MinimumDistance)
		{
			const FVector Direction = FRotator(0, Scratch.Last().Yaw, 0).Vector() * FMath::Sign(Control.MoveInput);
			for (int32 Index = 1; Index < Scratch.Num(); ++Index)
				Scratch[Index].Position += Direction * (MinimumDistance - Travel.Size2D()) * Index / Count;
		}
		TArray<FHull> Path;
		Path.Append(Scratch.GetData(), Scratch.Num());
		FResult Result = CheckPath(Path, Duration, Value(Settings.SafetyMargin, 150) + ExtraMargin);
		Result.ForecastAcceleration = Scratch[0].Acceleration;
		Result.ForecastYawRate = Scratch[0].YawRate;
		Result.ForecastYawAcceleration = Scratch[0].YawAcceleration;
		Result.LinearDamping = Scratch[0].Damping;
		Result.AngularDamping = Scratch[0].AngularDamping;
		Result.HullExtent = Scratch[0].Extent;
		return Result;
	}

	FResult FQuery::CheckTransform(const FTransform& Start, const FTransform& End) const
	{
		if (!bValid) return { false, EThreat::Invalid, nullptr };
		const float Angle = FMath::FindDeltaAngleDegrees(Start.Rotator().Yaw, End.Rotator().Yaw);
		const int32 Count = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Angle) / Value(Settings.SafetyMaximumYawStep, 5, 0.5f, 15)));
		if (Count > FMath::Clamp(Settings.SafetyMaximumSegments, 1, 64)) return { false, EThreat::Invalid, nullptr };
		TArray<FHull> Path;
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			FHull Pose = Hull;
			Pose.Position = FMath::Lerp(Start.GetLocation(), End.GetLocation(), static_cast<float>(Index) / Count);
			Pose.Yaw = Start.Rotator().Yaw + Angle * Index / Count;
			Path.Add(Pose);
		}
		return CheckPath(Path, 0.0f, Value(Settings.SafetyMargin, 150));
	}

	bool HasTerrainAhead(AEnemyShip& Ship, float Distance)
	{
		FHull Start;
		if (!MakeHull(Ship, {}, Start)) return false;
		FHull End = Start;
		End.Position += Ship.GetActorForwardVector().GetSafeNormal2D() * FMath::Max(1.0f, Distance);
		TArray<TWeakObjectPtr<AActor>> Water;
		for (TActorIterator<AWaterBody> It(Ship.GetWorld()); It; ++It) Water.Add(*It);
		return TerrainSegment(Ship, Start, End, 0, Water).Threat == EThreat::Terrain;
	}
}
