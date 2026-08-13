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
#include "Net/UnrealNetwork.h"
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

static TAutoConsoleVariable<int32> CVarSwimTransitionDebug(
	TEXT("p.SwimTransitionDebug"),
	0,
	TEXT("Log authoritative custom-swim surface and vertical-input state.\n")
	TEXT("0: Disabled\n")
	TEXT("1: Log while Ctrl/Space vertical swim input is active"),
	ECVF_Default
);

USwimmingComponent::USwimmingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void USwimmingComponent::SetVerticalSwimInput(float InVerticalInput)
{
	VerticalSwimInput = FMath::Clamp(InVerticalInput, -1.0f, 1.0f);
	bDiveInputHeld = VerticalSwimInput < -KINDA_SMALL_NUMBER;
	bAscendInputHeld = VerticalSwimInput > KINDA_SMALL_NUMBER;
	if (VerticalSwimInput < -KINDA_SMALL_NUMBER && IsCustomSwimming())
	{
		// Diving is a state transition, not just a temporary force. Releasing Ctrl
		// therefore keeps the player at their newly chosen depth.
		DepthMode = ESwimDepthMode::Submerged;
	}
}

bool USwimmingComponent::HasVerticalSwimInput() const
{
	return bDiveInputHeld || bAscendInputHeld;
}

bool USwimmingComponent::ShouldUseCameraDirectedUnderwaterMovement() const
{
	return IsCustomSwimming()
		&& bIsUnderwater
		&& DepthMode == ESwimDepthMode::Submerged
		&& !HasVerticalSwimInput();
}

bool USwimmingComponent::IsCustomSwimming() const
{
	return CharacterMovement
		&& CharacterMovement->MovementMode == MOVE_Custom
		&& CharacterMovement->CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming);
}

FSwimmingAnimationState USwimmingComponent::GetAnimationState() const
{
	FSwimmingAnimationState State;
	State.bIsSwimming = IsCustomSwimming();
	State.bIsUnderwater = bIsUnderwater;
	State.bDiveInputHeld = bDiveInputHeld;
	State.bAscendInputHeld = bAscendInputHeld;
	State.DepthMode = DepthMode;

	if (!OwnerCharacter || !CharacterMovement)
	{
		return State;
	}

	const FVector Velocity = CharacterMovement->Velocity;
	// During neutral underwater movement, W follows the camera pitch. Use total
	// speed so a steep upward/downward forward swim still selects a forward loop.
	State.HorizontalSpeed = ShouldUseCameraDirectedUnderwaterMovement()
		? Velocity.Size()
		: Velocity.Size2D();
	State.VerticalSpeed = Velocity.Z;

	const FVector LocalVelocity = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(Velocity);
	if (!LocalVelocity.IsNearlyZero())
	{
		State.Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	}

	return State;
}

void USwimmingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USwimmingComponent, bDiveInputHeld);
	DOREPLIFETIME(USwimmingComponent, bAscendInputHeld);
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

	bool bIsCustomSwimming = IsCustomSwimming();

	// If we are not swimming and not overlapping any water bodies, do not check transitions or query water height.
	if (!bIsCustomSwimming && OverlappingWaterBodies.Num() == 0)
	{
		bIsInShallowWater = false;
		return;
	}

	float CapsuleHalfHeight = CapsuleComponent->GetUnscaledCapsuleHalfHeight();
	FVector ActorLocation = OwnerCharacter->GetActorLocation();
	FVector FeetLocation = ActorLocation - FVector(0.f, 0.f, CapsuleHalfHeight);

	float FeetWaterHeight = -100000.f;
	bool bFeetInWater = GetWaterHeightAtLocation(FeetLocation, FeetWaterHeight);

	float FeetSubmersion = bFeetInWater ? (FeetWaterHeight - FeetLocation.Z) : -100000.f;
	const float CapsuleHeight = CapsuleHalfHeight * 2.0f;
	const float SwimEntryDepth = CapsuleHeight * SwimEntryCapsuleSubmersionRatio;
	const float SwimExitDepth = CapsuleHeight * SwimExitCapsuleSubmersionRatio;

	// Contact with water slows ground movement. Swimming remains a separate state.
	bIsInShallowWater = !bIsCustomSwimming && bFeetInWater && FeetSubmersion >= 0.0f;

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
		if (bFeetInWater && FeetSubmersion >= SwimEntryDepth)
		{
			CharacterMovement->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
			DepthMode = ESwimDepthMode::Surface;
			bIsInShallowWater = false;
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

		bool bExitSubmersion = !bFeetInWater || (FeetSubmersion < SwimExitDepth);

		if (bExitSubmersion && bOnWalkableFloor)
		{
			CharacterMovement->SetMovementMode(MOVE_Walking);
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Walking) (FeetSubmersion: %.2f, ExitDepth: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion, SwimExitDepth);
			bIsInShallowWater = bFeetInWater && FeetSubmersion >= 0.0f;
		}
		else if (!bFeetInWater || FeetSubmersion < 0.0f)
		{
			// 물높이가 감지되지 않거나 발밑이 물높이보다 위로 떠버린 경우 (완전히 뭍으로 탈출 또는 공중 점프 등)
			CharacterMovement->SetMovementMode(MOVE_Falling);
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Falling) (FeetSubmersion: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion);
		}
	}

	if (IsCustomSwimming())
	{
		UpdateUnderwaterState();
	}
	else
	{
		bIsUnderwater = false;
		if (!bFeetInWater)
		{
			bIsInShallowWater = false;
		}
		VerticalSwimInput = 0.0f;
		DepthMode = ESwimDepthMode::Surface;
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
	UpdateDepthMode();

	const float InputVerticalAcceleration = VerticalSwimInput * VerticalSwimAcceleration;
	const bool bHasVerticalInput = HasVerticalSwimInput();
	const bool bUseCameraDirectedMovement = ShouldUseCameraDirectedUnderwaterMovement();

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
	if (!bHasVerticalInput && DepthMode == ESwimDepthMode::Surface && bPontoonInWater)
	{
		// Follow the wave with a damped height spring. This replaces a permanent
		// upward force, so the surface position is stable instead of drifting up.
		const float TargetActorZ = WaterHeight - SurfaceTargetDepth;
		const float SurfaceAcceleration = (TargetActorZ - ActorLocation.Z) * SurfaceHeightSpring
			- CharacterMovement->Velocity.Z * SurfaceHeightDamping;
		CharacterMovement->Velocity.Z = FMath::Clamp(
			CharacterMovement->Velocity.Z + SurfaceAcceleration * DeltaTime,
			-MaxVerticalSwimSpeed,
			MaxVerticalSwimSpeed);
	}
	else if (bHasVerticalInput)
	{
		// Ctrl/Space own the full body. Do not retain a horizontal movement input
		// while a dedicated descend/ascend animation is playing.
		float NewVerticalVelocity = FMath::Clamp(
			CharacterMovement->Velocity.Z + InputVerticalAcceleration * DeltaTime,
			-MaxVerticalSwimSpeed,
			MaxVerticalSwimSpeed);

		if (VerticalSwimInput > KINDA_SMALL_NUMBER && bPontoonInWater)
		{
			// Space may bring the player to the surface, but it must not propel the
			// capsule through it. Use the same wave-aware target as surface swimming
			// and cap this frame's upward travel before SafeMoveUpdatedComponent.
			const float SurfaceTargetActorZ = WaterHeight - SurfaceTargetDepth;
			const float RemainingRise = SurfaceTargetActorZ - ActorLocation.Z;
			const float MaxUpwardVelocityToSurface = FMath::Max(0.0f, RemainingRise / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
			NewVerticalVelocity = FMath::Min(NewVerticalVelocity, MaxUpwardVelocityToSurface);
		}

		CharacterMovement->Velocity.Z = NewVerticalVelocity;
	}
	else if (!bUseCameraDirectedMovement)
	{
		// Underwater movement is neutrally buoyant. With no input, drag settles Z
		// velocity to zero and retains the depth selected by the player.
		CharacterMovement->Velocity.Z += InputVerticalAcceleration * DeltaTime;
		CharacterMovement->Velocity.Z = FMath::FInterpTo(
			CharacterMovement->Velocity.Z,
			0.0f,
			DeltaTime,
			SubmergedVerticalDamping);
		CharacterMovement->Velocity.Z = FMath::Clamp(
			CharacterMovement->Velocity.Z,
			-MaxVerticalSwimSpeed,
			MaxVerticalSwimSpeed);
	}

	if (bUseCameraDirectedMovement)
	{
		// Neutral underwater W movement follows the full control rotation, including
		// pitch. This keeps W/A/S/D diagonals while allowing the camera to steer depth.
		const FVector InputDirection = CharacterMovement->GetCurrentAcceleration().GetSafeNormal();
		FVector NewVelocity = CharacterMovement->Velocity
			+ (InputDirection * SwimAcceleration - CharacterMovement->Velocity * SwimFriction) * DeltaTime;
		CharacterMovement->Velocity = NewVelocity.GetClampedToMaxSize(MaxSwimSpeed);
	}
	else
	{
		// Surface movement and Ctrl/Space movement remain planar. While Ctrl/Space is
		// held, this applies drag only so the character transitions to vertical travel.
		const FVector InputDirection = bHasVerticalInput
			? FVector::ZeroVector
			: CharacterMovement->GetCurrentAcceleration().GetSafeNormal2D();
		const FVector CurrentHorizontalVelocity(CharacterMovement->Velocity.X, CharacterMovement->Velocity.Y, 0.f);
		FVector NewHorizontalVelocity = CurrentHorizontalVelocity
			+ (InputDirection * SwimAcceleration - CurrentHorizontalVelocity * SwimFriction) * DeltaTime;
		NewHorizontalVelocity = NewHorizontalVelocity.GetClampedToMaxSize(MaxSwimSpeed);
		CharacterMovement->Velocity.X = NewHorizontalVelocity.X;
		CharacterMovement->Velocity.Y = NewHorizontalVelocity.Y;
	}

	// 4. CMC 이동 및 충돌 슬라이딩 처리
	FHitResult SweepHit;
	CharacterMovement->SafeMoveUpdatedComponent(CharacterMovement->Velocity * DeltaTime, OwnerCharacter->GetActorRotation(), true, SweepHit);
	if (SweepHit.IsValidBlockingHit())
	{
		if (SweepHit.Normal.Z > 0.5f && CharacterMovement->Velocity.Z < 0.0f)
		{
			// In deep water, touching the sea floor should stop descent but retain swimming.
			CharacterMovement->Velocity.Z = 0.0f;
		}
		static_cast<UMovementComponent*>(CharacterMovement)->SlideAlongSurface(CharacterMovement->Velocity * DeltaTime, 1.f - SweepHit.Time, SweepHit.Normal, SweepHit, true);
	}

	// Use the exact same water query and target used for the Space movement cap.
	// Do not immediately overwrite this result with a second head-location query:
	// on steep waves that query can differ enough to leave the animation state in
	// Ascend after the movement has already reached the surface ceiling.
	const float SurfaceTargetActorZ = WaterHeight - SurfaceTargetDepth;
	const bool bReachedSurfaceWhileAscending = bPontoonInWater
		&& bAscendInputHeld
		&& OwnerCharacter->GetActorLocation().Z >= SurfaceTargetActorZ - 1.0f;
	if (bReachedSurfaceWhileAscending)
	{
		CharacterMovement->Velocity.Z = FMath::Min(CharacterMovement->Velocity.Z, 0.0f);
		DepthMode = ESwimDepthMode::Surface;
		bIsUnderwater = false;
	}
	else
	{
		UpdateUnderwaterState();
	}

	if (CVarSwimTransitionDebug.GetValueOnGameThread() != 0
		&& HasVerticalSwimInput()
		&& GetWorld()
		&& GetWorld()->GetTimeSeconds() - LastLoggedTime >= 0.25f)
	{
		LastLoggedTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogTemp, Warning,
			TEXT("[SwimTransition] %s Input=%.1f Dive=%d Ascend=%d ReachedSurface=%d Underwater=%d DepthMode=%d ActorZ=%.1f WaterZ=%.1f SurfaceTargetZ=%.1f VelZ=%.1f"),
			*GetNameSafe(OwnerCharacter),
			VerticalSwimInput,
			bDiveInputHeld ? 1 : 0,
			bAscendInputHeld ? 1 : 0,
			bReachedSurfaceWhileAscending ? 1 : 0,
			bIsUnderwater ? 1 : 0,
			static_cast<int32>(DepthMode),
			OwnerCharacter->GetActorLocation().Z,
			WaterHeight,
			SurfaceTargetActorZ,
			CharacterMovement->Velocity.Z);
	}
}

void USwimmingComponent::UpdateUnderwaterState()
{
	if (!OwnerCharacter || !CapsuleComponent || !IsCustomSwimming())
	{
		bIsUnderwater = false;
		return;
	}

	const FVector HeadLocation = OwnerCharacter->GetActorLocation()
		+ FVector::UpVector * (CapsuleComponent->GetUnscaledCapsuleHalfHeight() - 15.0f);
	float WaterHeight = -BIG_NUMBER;
	if (!GetWaterHeightAtLocation(HeadLocation, WaterHeight))
	{
		bIsUnderwater = false;
		return;
	}

	const float WaterHeightRelativeToHead = WaterHeight - HeadLocation.Z;
	if (bIsUnderwater)
	{
		// Waves may cross the exact head height every frame. Keep the state until
		// the head is clearly above the current wave surface.
		bIsUnderwater = WaterHeightRelativeToHead > -UnderwaterExitHeadClearance;
	}
	else
	{
		// Require meaningful submersion before entering the underwater state.
		bIsUnderwater = WaterHeightRelativeToHead > UnderwaterEntryHeadSubmersion;
	}
}

void USwimmingComponent::UpdateDepthMode()
{
	if (!OwnerCharacter || !CapsuleComponent || !IsCustomSwimming())
	{
		DepthMode = ESwimDepthMode::Surface;
		return;
	}

	if (VerticalSwimInput < -KINDA_SMALL_NUMBER)
	{
		DepthMode = ESwimDepthMode::Submerged;
		return;
	}

	if (DepthMode != ESwimDepthMode::Submerged)
	{
		return;
	}

	const FVector HeadLocation = OwnerCharacter->GetActorLocation()
		+ FVector::UpVector * (CapsuleComponent->GetUnscaledCapsuleHalfHeight() - 15.0f);
	float HeadWaterHeight = -BIG_NUMBER;
	if (GetWaterHeightAtLocation(HeadLocation, HeadWaterHeight)
		&& HeadLocation.Z >= HeadWaterHeight + SurfaceReentryHeadClearance)
	{
		DepthMode = ESwimDepthMode::Surface;
	}
}


