#include "SwimmingComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyActor.h"
#include "WaterBodyTypes.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "WaterWaves.h"
#include "WaterSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "RippleSubsystem.h"
#include "SWRippleWaterWaves.h"

static TAutoConsoleVariable<int32> CVarShowSwimBuoyancyDebug(
	TEXT("p.ShowSwimBuoyancyDebug"),
	0,
	TEXT("Show custom swimming buoyancy pontoon debug shapes.\n")
	TEXT("0: Disabled\n")
	TEXT("1: Enabled"),
	ECVF_Default
);

USwimmingComponent::USwimmingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USwimmingComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter)
	{
		CharacterMovement = OwnerCharacter->GetCharacterMovement();
		CapsuleComponent = OwnerCharacter->GetCapsuleComponent();

		if (CapsuleComponent)
		{
			// Ensure overlap events are enabled
			CapsuleComponent->SetGenerateOverlapEvents(true);
			
			// Ensure it overlaps with ECC_WorldStatic (often used by WaterBody collision)
			if (CapsuleComponent->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Ignore)
			{
				CapsuleComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
			}

			CapsuleComponent->OnComponentBeginOverlap.AddDynamic(this, &USwimmingComponent::OnOverlapBegin);
			CapsuleComponent->OnComponentEndOverlap.AddDynamic(this, &USwimmingComponent::OnOverlapEnd);

			InitializeOverlaps();
			
			// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] BeginPlay: Capsule setup complete. GenerateOverlapEvents=%s"), 
			// 	CapsuleComponent->GetGenerateOverlapEvents() ? TEXT("True") : TEXT("False"));
		}
	}
}

void USwimmingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Pontoon Debug Visualizer - Active only in editor (PIE)
#if WITH_EDITOR
	if (GIsEditor && OwnerCharacter)
#else
	if (false)
#endif
	{
		FVector ActorLocation = OwnerCharacter->GetActorLocation();
		FVector PontoonLocation = ActorLocation + PontoonOffset;

		float WaterHeight = -100000.f;
		FVector QueryLocation = PontoonLocation - FVector(0.f, 0.f, PontoonRadius + 100.f);
		bool bPontoonInWater = GetWaterHeightAtLocation(QueryLocation, WaterHeight);

		FColor SphereColor = FColor::Orange; // Orange: Not in water
		float Submersion = 0.f;

		if (bPontoonInWater)
		{
			Submersion = WaterHeight - (PontoonLocation.Z - PontoonRadius);
			if (Submersion > 0.f)
			{
				if (Submersion >= 2.f * PontoonRadius)
				{
					SphereColor = FColor::Blue; // Blue: Fully submerged
				}
				else
				{
					SphereColor = FColor::Green; // Green: Partially submerged
				}
			}
			else
			{
				SphereColor = FColor::Yellow; // Yellow: Above water surface but inside water body
			}
		}

		// Draw Pontoon sphere representing its size and position
		// DrawDebugSphere(GetWorld(), PontoonLocation, PontoonRadius, 16, SphereColor, false, -1.f, 0, 1.5f);

		// Draw center point
		// DrawDebugPoint(GetWorld(), PontoonLocation, 8.f, FColor::White, false, -1.f);

		// Draw water surface height and intersection if inside water
		if (bPontoonInWater)
		{
			FVector WaterSurfaceIntersection = FVector(PontoonLocation.X, PontoonLocation.Y, WaterHeight);

			// Vertical line from pontoon center to water surface
			// DrawDebugLine(GetWorld(), PontoonLocation, WaterSurfaceIntersection, FColor::Cyan, false, -1.f, 0, 1.5f);

			// Draw a horizontal cross at the water surface to show the intersection level
			// DrawDebugLine(GetWorld(), WaterSurfaceIntersection - FVector(PontoonRadius, 0.f, 0.f), WaterSurfaceIntersection + FVector(PontoonRadius, 0.f, 0.f), FColor::Cyan, false, -1.f, 0, 1.5f);
			// DrawDebugLine(GetWorld(), WaterSurfaceIntersection - FVector(0.f, PontoonRadius, 0.f), WaterSurfaceIntersection + FVector(0.f, PontoonRadius, 0.f), FColor::Cyan, false, -1.f, 0, 1.5f);
		}
	}
}

void USwimmingComponent::InitializeOverlaps()
{
	if (CapsuleComponent)
	{
		TArray<AActor*> OverlappingActors;
		CapsuleComponent->GetOverlappingActors(OverlappingActors);
		// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] InitializeOverlaps: Found %d overlapping actors in capsule on start."), OverlappingActors.Num());
		for (AActor* Actor : OverlappingActors)
		{
			if (AWaterBody* WaterBodyActor = Cast<AWaterBody>(Actor))
			{
				if (UWaterBodyComponent* WaterBody = WaterBodyActor->GetWaterBodyComponent())
				{
					OverlappingWaterBodies.AddUnique(WaterBody);
					LastActiveWaterBody = WaterBody;
					// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] InitializeOverlaps: Found WaterBody from Actor=%s"), *Actor->GetName());
				}
			}
		}
	}
}

void USwimmingComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor)
	{
		if (AWaterBody* WaterBodyActor = Cast<AWaterBody>(OtherActor))
		{
			if (UWaterBodyComponent* WaterBody = WaterBodyActor->GetWaterBodyComponent())
			{
				OverlappingWaterBodies.AddUnique(WaterBody);
				LastActiveWaterBody = WaterBody;
				// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Overlap Begin: WaterBody Actor=%s, Component=%s. Total water bodies=%d"), 
				// 	*OtherActor->GetName(), *WaterBody->GetName(), OverlappingWaterBodies.Num());
			}
		}
		else
		{
			// UE_LOG(LogTemp, Log, TEXT("[SwimDebug] Overlap Begin (Non-Water): Component=%s (Actor=%s)"), 
			// 	OtherComp ? *OtherComp->GetName() : TEXT("None"), *OtherActor->GetName());
		}
	}
}

void USwimmingComponent::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor)
	{
		if (AWaterBody* WaterBodyActor = Cast<AWaterBody>(OtherActor))
		{
			if (UWaterBodyComponent* WaterBody = WaterBodyActor->GetWaterBodyComponent())
			{
				// Check if we are still overlapping any components of this water body actor
				bool bStillOverlapping = false;
				if (CapsuleComponent)
				{
					TArray<UPrimitiveComponent*> OverlappingComps;
					CapsuleComponent->GetOverlappingComponents(OverlappingComps);
					for (UPrimitiveComponent* Comp : OverlappingComps)
					{
						if (Comp && Comp->GetOwner() == OtherActor)
						{
							bStillOverlapping = true;
							break;
						}
					}
				}

				if (!bStillOverlapping)
				{
					OverlappingWaterBodies.Remove(WaterBody);
					// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Overlap End: WaterBody Actor=%s. Total water bodies=%d"), 
					// 	*OtherActor->GetName(), OverlappingWaterBodies.Num());
				}
			}
		}
	}
}

bool USwimmingComponent::GetWaterHeightAtLocation(const FVector& Location, float& OutWaterHeight) const
{
	float MaxWaterHeight = -100000.f;
	bool bInWater = false;
	
	// Query 100cm below the location to handle being slightly above the surface (bobbing/jumping)
	FVector QueryLocation = Location - FVector(0.f, 0.f, 100.f);

	// Get Server Synchronized Time to fetch deterministic wave height
	float CurrentServerTime = 0.0f;
	if (GetWorld())
	{
		if (AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			CurrentServerTime = GameState->GetServerWorldTimeSeconds();
		}
		else
		{
			CurrentServerTime = GetWorld()->GetTimeSeconds();
		}
	}

	auto CheckWaterBody = [&](UWaterBodyComponent* WaterBody)
	{
		if (!WaterBody) return;

		// Compute flat water surface height first
		EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeLocation
										| EWaterBodyQueryFlags::ComputeDepth;

		float SplineInputKey = -1.f;
		if (WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			SplineInputKey = WaterBody->FindInputKeyClosestToWorldLocation(QueryLocation);
		}

		TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> QueryResult = WaterBody->TryQueryWaterInfoClosestToWorldLocation(QueryLocation, QueryFlags, SplineInputKey);
		if (QueryResult.HasValue())
		{
			const FWaterBodyQueryResult& Query = QueryResult.GetValue();
			float FlatWaterZ = Query.GetWaterSurfaceLocation().Z;
			float WaterZ = FlatWaterZ;

			// Add wave offset manually using the server-synchronized time
			if (WaterBody->HasWaves())
			{
				float WaterDepth = Query.GetWaterSurfaceDepth();
				if (UWaterWavesBase* WaterWaves = WaterBody->GetWaterWaves())
				{
					float AttenuationFactor = WaterWaves->GetWaveAttenuationFactor(Query.GetWaterSurfaceLocation(), WaterDepth, WaterBody->TargetWaveMaskDepth);
					if (AttenuationFactor > 0.0f)
					{
						FVector ComputedNormal;
						float RawWaveHeight = WaterWaves->GetWaveHeightAtPosition(Query.GetWaterSurfaceLocation(), WaterDepth, CurrentServerTime, ComputedNormal);
						WaterZ += RawWaveHeight * AttenuationFactor;

						// Fallback: If custom waves are not assigned, manually query and add the ripple height
						if (!WaterWaves->IsA<USWRippleWaterWaves>())
						{
							if (URippleSubsystem* RippleSubsystem = GetWorld()->GetSubsystem<URippleSubsystem>())
							{
								WaterZ += RippleSubsystem->GetRippleHeight(Query.GetWaterSurfaceLocation()) * AttenuationFactor;
							}
						}
					}
				}
			}

			// If query location is below the wave-calculated water surface, count as in water
			if (QueryLocation.Z <= WaterZ)
			{
				if (WaterZ > MaxWaterHeight)
				{
					MaxWaterHeight = WaterZ;
					bInWater = true;
				}
			}
		}
	};

	for (UWaterBodyComponent* WaterBody : OverlappingWaterBodies)
	{
		CheckWaterBody(WaterBody);
	}

	// Fallback to LastActiveWaterBody if not currently overlapping but we have a cached water body
	if (!bInWater && LastActiveWaterBody.IsValid())
	{
		CheckWaterBody(LastActiveWaterBody.Get());
	}

	OutWaterHeight = MaxWaterHeight;
	return bInWater;
}

void USwimmingComponent::CheckWaterTransitions()
{
	if (!OwnerCharacter || !CharacterMovement || !CapsuleComponent) return;

	bool bIsCustomSwimming = (CharacterMovement->MovementMode == MOVE_Custom &&
							  CharacterMovement->CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));

	// If we are not swimming and not overlapping any water bodies, do not check transitions or query water height.
	if (!bIsCustomSwimming && OverlappingWaterBodies.Num() == 0)
	{
		return;
	}

	float CapsuleHalfHeight = CapsuleComponent->GetUnscaledCapsuleHalfHeight();
	FVector ActorLocation = OwnerCharacter->GetActorLocation();
	FVector FeetLocation = ActorLocation - FVector(0.f, 0.f, CapsuleHalfHeight);

	float FeetWaterHeight = -100000.f;
	bool bFeetInWater = GetWaterHeightAtLocation(FeetLocation, FeetWaterHeight);

	float FeetSubmersion = bFeetInWater ? (FeetWaterHeight - FeetLocation.Z) : -100000.f;

	// Throttled logging (every 30 frames)
	// static int32 FrameCount = 0;
	// FrameCount++;
	// bool bShouldLog = (FrameCount % 30 == 0);

	// if (bShouldLog)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Transitions Check: OverlappingWaterBodies=%d | FeetLoc=%s | bFeetInWater=%s | FeetWaterHeight=%.2f | FeetSubmersion=%.2f | EntryOffset=%.2f | Mode=%d | CustomMode=%d"),
	// 		OverlappingWaterBodies.Num(),
	// 		*FeetLocation.ToString(),
	// 		bFeetInWater ? TEXT("True") : TEXT("False"),
	// 		FeetWaterHeight,
	// 		FeetSubmersion,
	// 		SwimEntryOffset,
	// 		(int32)CharacterMovement->MovementMode,
	// 		CharacterMovement->CustomMovementMode);
	// }

	if (!bIsCustomSwimming)
	{
		// Entry: 물 표면이 발밑에서부터 SwimEntryOffset 이상 깊어졌을 때 수영 상태 진입
		if (bFeetInWater && FeetSubmersion > SwimEntryOffset)
		{
			CharacterMovement->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
			CharacterMovement->Buoyancy = 0.f; // CMC의 기본 부력 사용 정지
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s >>> Entered Swimming State! (FeetSubmersion: %.2f)"), *ContextStr, *OwnerName, FeetSubmersion);
		}
	}
	else
	{
		// Exit: CMC 바닥 감지 시스템을 이용하여 바로 밑에 walkable floor가 있고 물 밖으로 오프셋만큼 나왔을 때
		FFindFloorResult FloorResult;
		CharacterMovement->FindFloor(ActorLocation, FloorResult, false);
		bool bOnWalkableFloor = FloorResult.IsWalkableFloor();

		float EffectiveExitOffset = FMath::Max(SwimExitOffset, SwimEntryOffset - 2.0f);
		bool bExitSubmersion = !bFeetInWater || (FeetSubmersion < EffectiveExitOffset);
		
		// if (bShouldLog)
		// {
		// 	UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Exit Check: bExitSubmersion=%s (Submersion=%.2f, ExitOffset=%.2f) | bOnWalkableFloor=%s"),
		// 		bExitSubmersion ? TEXT("True") : TEXT("False"),
		// 		FeetSubmersion,
		// 		SwimExitOffset,
		// 		bOnWalkableFloor ? TEXT("True") : TEXT("False"));
		// }

		if (bExitSubmersion && bOnWalkableFloor)
		{
			CharacterMovement->SetMovementMode(MOVE_Walking);
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Walking) (FeetSubmersion: %.2f, ExitOffset: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion, EffectiveExitOffset);
		}
		else if (!bFeetInWater || FeetSubmersion < -100.f)
		{
			// 물높이가 감지되지 않거나 발밑이 물높이보다 100cm 이상으로 떠버린 경우 (완전히 뭍으로 탈출 또는 공중 점프 등)
			CharacterMovement->SetMovementMode(MOVE_Falling);
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Falling) (FeetSubmersion: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion);
		}
	}
}

void USwimmingComponent::UpdateSwimmingMovement(float DeltaTime)
{
	if (!OwnerCharacter || !CharacterMovement) return;

	FVector ActorLocation = OwnerCharacter->GetActorLocation();
	FVector PontoonLocation = ActorLocation + PontoonOffset;

	// Pontoon 바닥면 부근에서 물 높이 쿼리 수행
	FVector QueryLocation = PontoonLocation - FVector(0.f, 0.f, PontoonRadius + 100.f);
	float WaterHeight = -100000.f;
	bool bPontoonInWater = GetWaterHeightAtLocation(QueryLocation, WaterHeight);

	float Mass = CharacterMovement->Mass;
	if (Mass <= 0.f) Mass = 100.f;

	// 1. Z축 중력 연산
	float GravityZ = GetWorld()->GetGravityZ() * CharacterMovement->GravityScale;

	// 2. Z축 부력 연산 (UBuoyancyComponent의 구체 체적 적분 및 댐핑 공식을 차용)
	float BuoyantForce = 0.f;
	float Submersion = 0.f;
	float SubVolume = 0.f;
	float DampingFactor = 0.f;

	if (bPontoonInWater)
	{
		Submersion = WaterHeight - (PontoonLocation.Z - PontoonRadius);
		if (Submersion > 0.f)
		{
			float SubDiff = FMath::Clamp(Submersion, 0.f, 2.f * PontoonRadius);
			float SubDiffSq = SubDiff * SubDiff;
			SubVolume = (PI / 3.f) * SubDiffSq * ((3.f * PontoonRadius) - SubDiff);

			float VelocityZ = CharacterMovement->Velocity.Z;
			float FirstOrderDrag = BuoyancyDamp * VelocityZ;
			float SecondOrderDrag = FMath::Sign(VelocityZ) * BuoyancyDamp2 * VelocityZ * VelocityZ;
			DampingFactor = -FMath::Max(FirstOrderDrag + SecondOrderDrag, 0.f);

			BuoyantForce = SubVolume * BuoyancyCoefficient + DampingFactor;
			BuoyantForce = FMath::Clamp(BuoyantForce, 0.f, MaxBuoyantForce);
		}
	}

	float BuoyantAccelerationZ = BuoyantForce / Mass;
	float TotalVertAccel = GravityZ + BuoyantAccelerationZ;

	// Throttled logging (every 30 frames)
	// static int32 FrameCount = 0;
	// FrameCount++;
	// bool bShouldLog = (FrameCount % 30 == 0);

	// if (bShouldLog)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Movement Update: bPontoonInWater=%s | PontoonLoc=%s | WaterHeight=%.2f | Submersion=%.2f | SubVolume=%.2f | DampFactor=%.2f | BuoyantForce=%.2f | BuoyantAccZ=%.2f | GravityZ=%.2f | TotalVertAccel=%.2f | CurrentVelZ=%.2f"),
	// 		bPontoonInWater ? TEXT("True") : TEXT("False"),
	// 		*PontoonLocation.ToString(),
	// 		WaterHeight,
	// 		Submersion,
	// 		SubVolume,
	// 		DampingFactor,
	// 		BuoyantForce,
	// 		BuoyantAccelerationZ,
	// 		GravityZ,
	// 		TotalVertAccel,
	// 		CharacterMovement->Velocity.Z);
	// }

	// 수직 속도 업데이트
	CharacterMovement->Velocity.Z += TotalVertAccel * DeltaTime;

	// 3. XY축 WASD 수평 이동 연산
	FVector InputDirection = CharacterMovement->GetCurrentAcceleration().GetSafeNormal2D();

	FVector CurrentHorizontalVelocity = FVector(CharacterMovement->Velocity.X, CharacterMovement->Velocity.Y, 0.f);
	FVector TargetAcceleration = InputDirection * SwimAcceleration;
	FVector FrictionForce = -CurrentHorizontalVelocity * SwimFriction;
	FVector NewHorizontalVelocity = CurrentHorizontalVelocity + (TargetAcceleration + FrictionForce) * DeltaTime;

	// MaxSwimSpeed로 속도 제한
	if (NewHorizontalVelocity.Size() > MaxSwimSpeed)
	{
		NewHorizontalVelocity = NewHorizontalVelocity.GetSafeNormal() * MaxSwimSpeed;
	}

	CharacterMovement->Velocity.X = NewHorizontalVelocity.X;
	CharacterMovement->Velocity.Y = NewHorizontalVelocity.Y;

	// 4. CMC 이동 및 충돌 슬라이딩 처리
	FHitResult SweepHit;
	CharacterMovement->SafeMoveUpdatedComponent(CharacterMovement->Velocity * DeltaTime, OwnerCharacter->GetActorRotation(), true, SweepHit);
	if (SweepHit.IsValidBlockingHit())
	{
		static_cast<UMovementComponent*>(CharacterMovement)->SlideAlongSurface(CharacterMovement->Velocity * DeltaTime, 1.f - SweepHit.Time, SweepHit.Normal, SweepHit, true);
	}
}


