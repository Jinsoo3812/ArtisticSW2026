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
#include "Water/SWBuoyancyMath.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WaterBodyCustomComponent.h"
#endif

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

namespace
{
	float ComputeSurfaceVerticalDragForce(
		float BodyVelocityZ,
		float WaterSurfaceVelocityZ,
		const FSWBuoyancyForceSettings& Settings,
		float WaterVelocityInfluence,
		float DragScale)
	{
		const float RelativeVelocityZ = BodyVelocityZ
			- WaterSurfaceVelocityZ * FMath::Clamp(WaterVelocityInfluence, 0.0f, 1.0f);
		const float LinearDrag = Settings.BuoyancyDamp * RelativeVelocityZ;
		const float QuadraticDrag = FMath::Sign(RelativeVelocityZ)
			* Settings.BuoyancyDamp2
			* FMath::Square(RelativeVelocityZ);
		return -(LinearDrag + QuadraticDrag) * FMath::Max(DragScale, 0.0f);
	}

	float ComputeSurfacePostureBlend(float HorizontalSpeed, float MovingPoseSpeed)
	{
		const float NormalizedSpeed = FMath::Clamp(
			HorizontalSpeed / FMath::Max(MovingPoseSpeed, 1.0f),
			0.0f,
			1.0f);
		return FMath::SmoothStep(0.0f, 1.0f, NormalizedSpeed);
	}

	void RemoveTrackedWaterBody(
		TArray<TObjectPtr<UWaterBodyComponent>>& OverlappingWaterBodies,
		TWeakObjectPtr<UWaterBodyComponent>& LastActiveWaterBody,
		UWaterBodyComponent* WaterBody,
		bool bPreserveActiveWaterBody = false)
	{
		OverlappingWaterBodies.Remove(WaterBody);

		if (LastActiveWaterBody.Get() != WaterBody)
		{
			return;
		}
		if (bPreserveActiveWaterBody)
		{
			return;
		}

		LastActiveWaterBody.Reset();
		for (int32 Index = OverlappingWaterBodies.Num() - 1; Index >= 0; --Index)
		{
			if (UWaterBodyComponent* RemainingWaterBody = OverlappingWaterBodies[Index])
			{
				LastActiveWaterBody = RemainingWaterBody;
				break;
			}
		}
	}
}

USwimmingComponent::USwimmingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);

	// Preserve the player tuning that was used before surface swimming changed
	// to a height spring. The setting type and solver are shared with ships/chests.
	BuoyancyForceSettings.BuoyancyCoefficient = 0.3f;
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

float USwimmingComponent::GetSurfacePostureBlend() const
{
	return CharacterMovement
		? ComputeSurfacePostureBlend(CharacterMovement->Velocity.Size2D(), SurfaceMovingPoseSpeed)
		: 0.0f;
}

FVector USwimmingComponent::GetSurfacePontoonOffset() const
{
	return FMath::Lerp(
		SurfaceIdlePontoonOffset,
		SurfaceMovingPontoonOffset,
		GetSurfacePostureBlend());
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
	// The owning client predicts these through CMC. Replication is only needed by
	// simulated proxies for their descend/ascend animation states.
	DOREPLIFETIME_CONDITION(USwimmingComponent, bDiveInputHeld, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(USwimmingComponent, bAscendInputHeld, COND_SkipOwner);
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
		FVector PontoonLocation = ActorLocation + GetSurfacePontoonOffset();

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
					const bool bPreserveActiveWaterBody = IsCustomSwimming()
						&& LastActiveWaterBody.Get() == WaterBody;
					RemoveTrackedWaterBody(
						OverlappingWaterBodies,
						LastActiveWaterBody,
						WaterBody,
						bPreserveActiveWaterBody);
					// UE_LOG(LogTemp, Warning, TEXT("[SwimDebug] Overlap End: WaterBody Actor=%s. Total water bodies=%d"), 
					// 	*OtherActor->GetName(), OverlappingWaterBodies.Num());
				}
			}
		}
	}
}

bool USwimmingComponent::GetWaterHeightAtLocation(
	const FVector& Location,
	float& OutWaterHeight,
	bool* bOutHadValidWaterBodyQuery,
	float WaveTimeOffsetSeconds) const
{
	float MaxValidWaterHeight = -100000.f;
	float MaxWetWaterHeight = -100000.f;
	bool bInWater = false;
	bool bHadValidWaterBodyQuery = false;
	
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
		CurrentServerTime += WaveTimeOffsetSeconds;
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
			if (Query.IsInExclusionVolume())
			{
				return;
			}
			bHadValidWaterBodyQuery = true;
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

			if (WaterZ > MaxValidWaterHeight)
			{
				MaxValidWaterHeight = WaterZ;
			}

			// Collision overlap only discovers a candidate body. The wave-aware query
			// decides whether this location is actually wet.
			if (QueryLocation.Z <= WaterZ)
			{
				bInWater = true;
				MaxWetWaterHeight = FMath::Max(MaxWetWaterHeight, WaterZ);
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

	OutWaterHeight = bInWater ? MaxWetWaterHeight : MaxValidWaterHeight;
	if (bOutHadValidWaterBodyQuery)
	{
		*bOutHadValidWaterBodyQuery = bHadValidWaterBodyQuery;
	}
	return bInWater;
}

void USwimmingComponent::CheckWaterTransitions(float DeltaSeconds)
{
	if (!OwnerCharacter || !CharacterMovement || !CapsuleComponent) return;

	bool bIsCustomSwimming = IsCustomSwimming();

	// If we are not swimming and have no overlapping water bodies AND no cached active water body, do not check transitions.
	if (!bIsCustomSwimming && OverlappingWaterBodies.Num() == 0 && !LastActiveWaterBody.IsValid())
	{
		bIsInShallowWater = false;
		return;
	}

	float CapsuleHalfHeight = CapsuleComponent->GetUnscaledCapsuleHalfHeight();
	FVector ActorLocation = OwnerCharacter->GetActorLocation();
	FVector FeetLocation = ActorLocation - FVector(0.f, 0.f, CapsuleHalfHeight);

	float FeetWaterHeight = -100000.f;
	bool bHadValidWaterBodyQuery = false;
	bool bFeetInWater = GetWaterHeightAtLocation(
		FeetLocation,
		FeetWaterHeight,
		&bHadValidWaterBodyQuery);

	float FeetSubmersion = bHadValidWaterBodyQuery
		? (FeetWaterHeight - FeetLocation.Z)
		: -100000.f;
	const float CapsuleHeight = CapsuleHalfHeight * 2.0f;
	const float SwimEntryDepth = CapsuleHeight * SwimEntryCapsuleSubmersionRatio;
	const float SwimExitDepth = CapsuleHeight * SwimExitCapsuleSubmersionRatio;

	// Contact with water slows ground movement. Swimming remains a separate state.
	bIsInShallowWater = !bIsCustomSwimming && bFeetInWater && FeetSubmersion >= 0.0f;

	if (!bIsCustomSwimming)
	{
		WaterQueryFailureElapsed = 0.0f;
		// Entry: 물 표면이 발밑에서부터 SwimEntryDepth 이상 깊어졌을 때 수영 상태 진입
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
		if (bHadValidWaterBodyQuery)
		{
			WaterQueryFailureElapsed = 0.0f;
		}
		else
		{
			WaterQueryFailureElapsed += FMath::Max(DeltaSeconds, 0.0f);
			if (WaterQueryFailureElapsed < WaterQueryFailureGraceTime)
			{
				// Query failure is an unknown state, not proof that the character is dry.
				// Keep the current movement mode while the active WaterBody lease is valid.
				return;
			}
		}

		// Exit: CMC 바닥 감지 시스템을 이용하여 바로 밑에 walkable floor가 있고 물 밖으로 오프셋만큼 나왔을 때
		FFindFloorResult FloorResult;
		CharacterMovement->FindFloor(ActorLocation, FloorResult, false);
		bool bOnWalkableFloor = FloorResult.IsWalkableFloor();

		bool bExitSubmersion = !bFeetInWater || (FeetSubmersion < SwimExitDepth);

		if (bExitSubmersion && bOnWalkableFloor)
		{
			CharacterMovement->SetMovementMode(MOVE_Walking);
			LastActiveWaterBody.Reset();
			WaterQueryFailureElapsed = 0.0f;
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Walking) (FeetSubmersion: %.2f, ExitDepth: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion, SwimExitDepth);
			bIsInShallowWater = bFeetInWater && FeetSubmersion >= 0.0f;
		}
		else if (!bFeetInWater || FeetSubmersion < -100.0f)
		{
			// 물높이가 전혀 감지되지 않거나 발밑이 수면 위 100cm 이상 완전히 공중으로 점프/이탈한 경우에만 Falling 전환
			CharacterMovement->SetMovementMode(MOVE_Falling);
			LastActiveWaterBody.Reset();
			WaterQueryFailureElapsed = 0.0f;
			
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
	FVector PontoonLocation = ActorLocation + GetSurfacePontoonOffset();

	// Pontoon 바닥면 부근에서 물 높이 쿼리 수행
	FVector QueryLocation = PontoonLocation - FVector(0.f, 0.f, PontoonRadius + 100.f);
	float WaterHeight = -100000.f;
	bool bPontoonInWater = GetWaterHeightAtLocation(QueryLocation, WaterHeight);
	UpdateDepthMode();

	const float InputVerticalAcceleration = VerticalSwimInput * VerticalSwimAcceleration;
	const bool bHasVerticalInput = HasVerticalSwimInput();
	const bool bUseCameraDirectedMovement = ShouldUseCameraDirectedUnderwaterMovement();

	if (!bHasVerticalInput && DepthMode == ESwimDepthMode::Surface)
	{
		FSWBuoyancySolveResult SolveResult;
		float WaterSurfaceVelocityZ = 0.0f;
		if (bPontoonInWater)
		{
			// A fixed interval keeps the sampled wave velocity independent of the
			// client/server frame rate used to evaluate the same CMC move.
			constexpr float SampleDeltaTime = 1.0f / 60.0f;
			float PreviousWaterHeight = WaterHeight;
			bool bHadPreviousWaterQuery = false;
			GetWaterHeightAtLocation(
				QueryLocation,
				PreviousWaterHeight,
				&bHadPreviousWaterQuery,
				-SampleDeltaTime);
			if (bHadPreviousWaterQuery)
			{
				WaterSurfaceVelocityZ = (WaterHeight - PreviousWaterHeight) / SampleDeltaTime;
			}

			FSWBuoyancySolveInput SolveInput;
			SolveInput.WaterHeight = WaterHeight;
			SolveInput.PontoonCenterZ = PontoonLocation.Z;
			SolveInput.PontoonRadius = PontoonRadius;
			SolveInput.RelativeVelocityZ = 0.0f;
			SolveInput.ForceScale = PontoonForceScale;

			// Keep the shared spherical-volume buoyancy, but apply player surface drag
			// separately so it can oppose motion in both vertical directions relative
			// to the moving wave surface.
			FSWBuoyancyForceSettings HydrostaticSettings = BuoyancyForceSettings;
			HydrostaticSettings.BuoyancyDamp = 0.0f;
			HydrostaticSettings.BuoyancyDamp2 = 0.0f;
			SolveResult = FSWBuoyancyMath::SolvePontoon(SolveInput, HydrostaticSettings);
		}

		const float Mass = FMath::Max(CharacterMovement->Mass, UE_SMALL_NUMBER);
		const float GravityAcceleration = GetWorld()
			? GetWorld()->GetGravityZ() * CharacterMovement->GravityScale
			: 0.0f;
		const float VerticalDragForce = SolveResult.bIsInWater
			? ComputeSurfaceVerticalDragForce(
				CharacterMovement->Velocity.Z,
				WaterSurfaceVelocityZ,
				BuoyancyForceSettings,
				SurfaceWaterVelocityInfluence,
				SurfaceVerticalDragScale)
			: 0.0f;
		const float MaxWaterForce = FMath::Max(BuoyancyForceSettings.MaxBuoyantForce, 0.0f);
		const float TotalWaterForceZ = FMath::Clamp(
			SolveResult.BuoyantForceZ + VerticalDragForce,
			-MaxWaterForce,
			MaxWaterForce);
		const float WaterAcceleration = TotalWaterForceZ / Mass;
		CharacterMovement->Velocity.Z = FMath::Clamp(
			CharacterMovement->Velocity.Z + (GravityAcceleration + WaterAcceleration) * DeltaTime,
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
			// capsule through the wave-aware surface target.
			const float SurfaceTargetActorZ = WaterHeight - SurfaceTargetDepth;
			const float RemainingRise = SurfaceTargetActorZ - ActorLocation.Z;
			const float MaxUpwardVelocityToSurface = FMath::Max(
				0.0f,
				RemainingRise / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
			NewVerticalVelocity = FMath::Min(NewVerticalVelocity, MaxUpwardVelocityToSurface);
		}

		CharacterMovement->Velocity.Z = NewVerticalVelocity;
	}
	else if (!bUseCameraDirectedMovement)
	{
		// Underwater neutral movement: drag settles Z velocity to zero
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
		// Neutral underwater W movement follows full control rotation, including pitch
		const FVector InputDirection = CharacterMovement->GetCurrentAcceleration().GetSafeNormal();
		FVector NewVelocity = CharacterMovement->Velocity
			+ (InputDirection * SwimAcceleration - CharacterMovement->Velocity * SwimFriction) * DeltaTime;
		CharacterMovement->Velocity = NewVelocity.GetClampedToMaxSize(MaxSwimSpeed);
	}
	else
	{
		// Surface movement and horizontal swim
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

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSwimmingSurfaceVerticalDragTest,
	"ArtisticSW.Swimming.SurfaceVerticalDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingSurfaceVerticalDragTest::RunTest(const FString& Parameters)
{
	FSWBuoyancyForceSettings Settings;
	Settings.BuoyancyDamp = 1000.0f;
	Settings.BuoyancyDamp2 = 1.0f;

	const float UpwardDrag = ComputeSurfaceVerticalDragForce(
		100.0f, 0.0f, Settings, 1.0f, 1.0f);
	const float DownwardDrag = ComputeSurfaceVerticalDragForce(
		-100.0f, 0.0f, Settings, 1.0f, 1.0f);
	TestEqual(TEXT("Upward motion receives downward drag"), UpwardDrag, -110000.0f);
	TestEqual(TEXT("Downward motion receives equal upward drag"), DownwardDrag, 110000.0f);

	const float WaveMatchedDrag = ComputeSurfaceVerticalDragForce(
		60.0f, 100.0f, Settings, 0.6f, 1.0f);
	TestTrue(TEXT("Matching the inherited wave velocity produces no drag"),
		FMath::IsNearlyZero(WaveMatchedDrag, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSwimmingSurfacePosturePontoonTest,
	"ArtisticSW.Swimming.SurfacePosturePontoon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingSurfacePosturePontoonTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Idle speed selects the upright pontoon"),
		ComputeSurfacePostureBlend(0.0f, 200.0f), 0.0f);
	TestEqual(TEXT("Half speed is the midpoint of the smooth blend"),
		ComputeSurfacePostureBlend(100.0f, 200.0f), 0.5f);
	TestEqual(TEXT("Moving speed selects the prone pontoon"),
		ComputeSurfacePostureBlend(200.0f, 200.0f), 1.0f);
	TestEqual(TEXT("The blend clamps speeds above the moving threshold"),
		ComputeSurfacePostureBlend(400.0f, 200.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSwimmingWaterBodyTrackingTest,
	"ArtisticSW.Swimming.WaterBodyTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingWaterBodyTrackingTest::RunTest(const FString& Parameters)
{
	UWaterBodyComponent* FirstWaterBody = NewObject<UWaterBodyCustomComponent>();
	UWaterBodyComponent* SecondWaterBody = NewObject<UWaterBodyCustomComponent>();
	UWaterBodyComponent* ThirdWaterBody = NewObject<UWaterBodyCustomComponent>();

	TArray<TObjectPtr<UWaterBodyComponent>> LeasedWaterBodies{ FirstWaterBody };
	TWeakObjectPtr<UWaterBodyComponent> LeasedActiveWaterBody = FirstWaterBody;
	RemoveTrackedWaterBody(
		LeasedWaterBodies,
		LeasedActiveWaterBody,
		FirstWaterBody,
		true);
	TestEqual(TEXT("A leased water body leaves the overlap candidate list"), LeasedWaterBodies.Num(), 0);
	TestTrue(TEXT("Swimming preserves the active water body lease after overlap loss"),
		LeasedActiveWaterBody.Get() == FirstWaterBody);
	RemoveTrackedWaterBody(LeasedWaterBodies, LeasedActiveWaterBody, FirstWaterBody);
	TestFalse(TEXT("A confirmed exit clears the active water body lease"),
		LeasedActiveWaterBody.IsValid());

	TArray<TObjectPtr<UWaterBodyComponent>> OverlappingWaterBodies{
		FirstWaterBody,
		SecondWaterBody,
		ThirdWaterBody
	};
	TWeakObjectPtr<UWaterBodyComponent> LastActiveWaterBody = ThirdWaterBody;

	RemoveTrackedWaterBody(OverlappingWaterBodies, LastActiveWaterBody, SecondWaterBody);
	TestEqual(TEXT("Ending a non-active overlap removes only that water body"), OverlappingWaterBodies.Num(), 2);
	TestTrue(TEXT("Ending a non-active overlap preserves the active water body"),
		LastActiveWaterBody.Get() == ThirdWaterBody);

	RemoveTrackedWaterBody(OverlappingWaterBodies, LastActiveWaterBody, ThirdWaterBody);
	TestEqual(TEXT("Ending the active overlap removes it"), OverlappingWaterBodies.Num(), 1);
	TestTrue(TEXT("Another overlapping water body becomes active"),
		LastActiveWaterBody.Get() == FirstWaterBody);

	RemoveTrackedWaterBody(OverlappingWaterBodies, LastActiveWaterBody, FirstWaterBody);
	TestEqual(TEXT("Ending the final overlap empties the tracked list"), OverlappingWaterBodies.Num(), 0);
	TestFalse(TEXT("Ending the final overlap clears the fallback water body"),
		LastActiveWaterBody.IsValid());

	return true;
}

#endif


