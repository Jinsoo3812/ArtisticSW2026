#include "SwimmingComponent.h"
#include "SWCabinWaterCullComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyActor.h"
#include "WaterBodyTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "WaterWaves.h"
#include "WaterSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Net/UnrealNetwork.h"
#include "RippleSubsystem.h"
#include "SWRippleWaterWaves.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"

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

static TAutoConsoleVariable<int32> CVarShowCabinSwimCullDebug(
	TEXT("p.ShowCabinSwimCullDebug"),
	1,
	TEXT("Show the local player's cabin swim-cull state in the top-left corner.\n")
	TEXT("0: Disabled\n")
	TEXT("1: Enabled"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarCabinSwimTrace(
	TEXT("p.CabinSwimTrace"),
	0,
	TEXT("Write detailed cabin-mask, water, floor, movement-mode, and vertical-velocity samples to the log.\n")
	TEXT("0: Disabled\n")
	TEXT("1: Enabled"),
	ECVF_Default
);

namespace
{
	float ComputeSurfaceFollowAcceleration(
		float PositionError,
		float VelocityError,
		float FrequencyHz,
		float DampingRatio,
		float MaxAcceleration)
	{
		const float Omega = 2.0f * UE_PI * FMath::Max(FrequencyHz, UE_SMALL_NUMBER);
		return FMath::Clamp(
			Omega * Omega * PositionError + 2.0f * FMath::Max(DampingRatio, 0.0f) * Omega * VelocityError,
			-FMath::Max(MaxAcceleration, 0.0f),
			FMath::Max(MaxAcceleration, 0.0f));
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
	const float ClampedInput = FMath::Clamp(InVerticalInput, -1.0f, 1.0f);
	SetRawVerticalSwimInput(
		ClampedInput < -KINDA_SMALL_NUMBER,
		ClampedInput > KINDA_SMALL_NUMBER);
}

void USwimmingComponent::SetRawVerticalSwimInput(bool bDiveHeld, bool bAscendHeld)
{
	if (!bDiveHeld)
	{
		bDiveInputSuppressedUntilRelease = false;
	}
	if (!bAscendHeld)
	{
		bAscendInputSuppressedUntilRelease = false;
	}

	const bool bNewDivePress = bDiveHeld && !bRawDiveInputHeld;
	bRawDiveInputHeld = bDiveHeld;
	bRawAscendInputHeld = bAscendHeld;

	if (MovementState == ESwimMovementState::SurfaceTransition)
	{
		bDiveInputSuppressedUntilRelease |= bDiveHeld;
		bAscendInputSuppressedUntilRelease |= bAscendHeld;
	}
	else if (MovementState == ESwimMovementState::DiveTransition)
	{
		bDiveInputSuppressedUntilRelease |= bNewDivePress;
		bAscendInputSuppressedUntilRelease |= bAscendHeld;
	}
	else if (MovementState == ESwimMovementState::Surface)
	{
		bAscendInputSuppressedUntilRelease |= bAscendHeld;
	}

	RefreshEffectiveVerticalInput();
}

void USwimmingComponent::RefreshEffectiveVerticalInput()
{
	const bool bAllowDive = bRawDiveInputHeld && !bDiveInputSuppressedUntilRelease;
	const bool bAllowAscend = bRawAscendInputHeld && !bAscendInputSuppressedUntilRelease;
	RawVerticalSwimInput = bAllowDive ? -1.0f : (bAllowAscend ? 1.0f : 0.0f);
	if (MovementState == ESwimMovementState::Surface || IsTransitionState())
	{
		RawVerticalSwimInput = MovementState == ESwimMovementState::DiveTransition && bAllowDive
			? -1.0f
			: 0.0f;
	}
	const float EffectiveInput = GetEffectiveVerticalSwimInput();
	bDiveInputHeld = EffectiveInput < -KINDA_SMALL_NUMBER;
	bAscendInputHeld = EffectiveInput > KINDA_SMALL_NUMBER;
}

float USwimmingComponent::GetEffectiveVerticalSwimInput() const
{
	switch (MovementState)
	{
	case ESwimMovementState::DiveTransition: return -1.0f;
	case ESwimMovementState::SurfaceTransition: return 1.0f;
	case ESwimMovementState::Submerged: return RawVerticalSwimInput;
	default: return 0.0f;
	}
}

bool USwimmingComponent::HasVerticalSwimInput() const
{
	return !FMath::IsNearlyZero(GetEffectiveVerticalSwimInput());
}

bool USwimmingComponent::IsTransitionState() const
{
	return MovementState == ESwimMovementState::DiveTransition
		|| MovementState == ESwimMovementState::SurfaceTransition;
}

bool USwimmingComponent::ShouldUseCameraDirectedUnderwaterMovement() const
{
	return IsCustomSwimming()
		&& bIsUnderwater
		&& MovementState == ESwimMovementState::Submerged
		&& !HasVerticalSwimInput();
}

bool USwimmingComponent::IsCustomSwimming() const
{
	return CharacterMovement
		&& CharacterMovement->MovementMode == MOVE_Custom
		&& CharacterMovement->CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming);
}

FSwimPredictionState USwimmingComponent::GetPredictionState() const
{
	FSwimPredictionState State;
	State.MovementState = MovementState;
	State.DiveTransitionElapsed = DiveTransitionElapsed;
	State.SurfaceTransitionElapsed = SurfaceTransitionElapsed;
	State.SurfaceTransitionStallElapsed = SurfaceTransitionStallElapsed;
	State.SurfaceTransitionEntryHoldElapsed = SurfaceTransitionEntryHoldElapsed;
	State.LastSurfaceTransitionProgressDepth = LastSurfaceTransitionProgressDepth;
	State.bRawDiveInputHeld = bRawDiveInputHeld;
	State.bRawAscendInputHeld = bRawAscendInputHeld;
	State.bDiveInputSuppressedUntilRelease = bDiveInputSuppressedUntilRelease;
	State.bAscendInputSuppressedUntilRelease = bAscendInputSuppressedUntilRelease;
	return State;
}

void USwimmingComponent::RestorePredictedSwimState(const FSwimPredictionState& InState)
{
	MovementState = InState.MovementState;
	DiveTransitionElapsed = InState.DiveTransitionElapsed;
	SurfaceTransitionElapsed = InState.SurfaceTransitionElapsed;
	SurfaceTransitionStallElapsed = InState.SurfaceTransitionStallElapsed;
	SurfaceTransitionEntryHoldElapsed = InState.SurfaceTransitionEntryHoldElapsed;
	LastSurfaceTransitionProgressDepth = InState.LastSurfaceTransitionProgressDepth;
	bRawDiveInputHeld = InState.bRawDiveInputHeld;
	bRawAscendInputHeld = InState.bRawAscendInputHeld;
	bDiveInputSuppressedUntilRelease = InState.bDiveInputSuppressedUntilRelease;
	bAscendInputSuppressedUntilRelease = InState.bAscendInputSuppressedUntilRelease;
	RefreshEffectiveVerticalInput();
}

ESwimDepthMode USwimmingComponent::ToAnimationDepthMode(ESwimMovementState InMovementState)
{
	return InMovementState == ESwimMovementState::Surface
		? ESwimDepthMode::Surface
		: ESwimDepthMode::Submerged;
}

ESwimDepthMode USwimmingComponent::GetDepthMode() const
{
	return ToAnimationDepthMode(MovementState);
}

FSwimmingAnimationState USwimmingComponent::GetAnimationState() const
{
	FSwimmingAnimationState State;
	State.bIsSwimming = IsCustomSwimming();
	State.bIsUnderwater = bIsUnderwater;
	State.bDiveInputHeld = bDiveInputHeld;
	State.bAscendInputHeld = bAscendInputHeld;
	State.DepthMode = ToAnimationDepthMode(MovementState);

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
	DOREPLIFETIME_CONDITION(USwimmingComponent, MovementState, COND_SkipOwner);
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

void USwimmingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ApplySwimmingGameplayState(false);
	Super::EndPlay(EndPlayReason);
}

void USwimmingComponent::ApplySwimmingGameplayState(bool bEntering)
{
	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<ACharacter>(GetOwner());
	}
	if (!OwnerCharacter)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
	if (!ASC)
	{
		return;
	}

	if (bEntering)
	{
		if (!ASC->HasMatchingGameplayTag(State_Swimming))
		{
			ASC->AddLooseGameplayTag(State_Swimming);
		}

		// Cancel ongoing offensive, skill, and roll abilities upon entering swimming
		FGameplayTagContainer CancelTags;
		CancelTags.AddTag(GameplayAbility_BasicAttack);
		CancelTags.AddTag(GameplayAbility_Weapon_AimCycle);
		CancelTags.AddTag(GameplayAbility_InterruptibleByHit);
		CancelTags.AddTag(GameplayAbility_Skill_GravityVortex);
		CancelTags.AddTag(GameplayAbility_Skill_WaterBomb);
		CancelTags.AddTag(GameplayAbility_Skill_Bombardment);
		CancelTags.AddTag(GameplayAbility_Player_Roll);
		ASC->CancelAbilities(&CancelTags);

		// Clean up aim-related loose tags if present
		ASC->RemoveLooseGameplayTag(State_Aiming);
		ASC->RemoveLooseGameplayTag(State_Sniping);
		ASC->RemoveLooseGameplayTag(State_Bow_Drawing);
		ASC->RemoveLooseGameplayTag(State_Bow_FullyDrawn);
		ASC->RemoveLooseGameplayTag(State_Bow_Releasing);

		if (ABasePlayer* Player = Cast<ABasePlayer>(OwnerCharacter))
		{
			Player->ResetConsumableQuickSlotInputs();
		}
	}
	else
	{
		if (ASC->HasMatchingGameplayTag(State_Swimming))
		{
			ASC->RemoveLooseGameplayTag(State_Swimming);
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
		const FVector ActorLocation = OwnerCharacter->GetActorLocation();
		const FSwimWaterSurfaceSample Sample = QueryWaterSurfaceSample(ActorLocation);
		const bool bPontoonInWater = Sample.bIsValid && ActorLocation.Z <= Sample.SurfaceZ;
		const float WaterHeight = Sample.SurfaceZ;
		const FVector PontoonLocation = ActorLocation;

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
		DrawDebugSphere(GetWorld(), PontoonLocation, PontoonRadius, 16, SphereColor, false, -1.f, 0, 1.5f);

		// Draw center point
		DrawDebugPoint(GetWorld(), PontoonLocation, 8.f, FColor::White, false, -1.f);

		if (Sample.bIsValid)
		{
			const FVector WaterSurfaceIntersection(PontoonLocation.X, PontoonLocation.Y, WaterHeight);
			const FVector SurfaceTarget(PontoonLocation.X, PontoonLocation.Y, WaterHeight - SurfaceTargetDepth);
			const FVector SubmergedThreshold(PontoonLocation.X, PontoonLocation.Y, WaterHeight - SubmergedDepthThreshold);

			// Vertical line from pontoon center to water surface
			DrawDebugLine(GetWorld(), PontoonLocation, WaterSurfaceIntersection, FColor::Cyan, false, -1.f, 0, 1.5f);

			// Draw a horizontal cross at the water surface to show the intersection level
			DrawDebugLine(GetWorld(), WaterSurfaceIntersection - FVector(PontoonRadius, 0.f, 0.f), WaterSurfaceIntersection + FVector(PontoonRadius, 0.f, 0.f), FColor::Cyan, false, -1.f, 0, 1.5f);
			DrawDebugLine(GetWorld(), WaterSurfaceIntersection - FVector(0.f, PontoonRadius, 0.f), WaterSurfaceIntersection + FVector(0.f, PontoonRadius, 0.f), FColor::Cyan, false, -1.f, 0, 1.5f);
			DrawDebugLine(GetWorld(), SurfaceTarget - FVector(PontoonRadius, 0.f, 0.f), SurfaceTarget + FVector(PontoonRadius, 0.f, 0.f), FColor::Green, false, -1.f, 0, 1.5f);
			DrawDebugLine(GetWorld(), SubmergedThreshold - FVector(PontoonRadius, 0.f, 0.f), SubmergedThreshold + FVector(PontoonRadius, 0.f, 0.f), FColor::Blue, false, -1.f, 0, 1.5f);
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
					const bool bPreserveActiveWaterBody = LastActiveWaterBody.Get() == WaterBody
						&& (IsCustomSwimming() || bWasInsideCabinWaterCull);
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

FSwimWaterSurfaceSample USwimmingComponent::QueryWaterSurfaceSample(const FVector& Location) const
{
	FSwimWaterSurfaceSample BestSample;
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

	auto EvaluateWaterBody = [&](UWaterBodyComponent* WaterBody, float SampleTime, float& OutZ, FVector& OutNormal)
	{
		if (!WaterBody)
		{
			return false;
		}
		EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeLocation
										| EWaterBodyQueryFlags::ComputeDepth;
		float SplineInputKey = -1.f;
		if (WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			SplineInputKey = WaterBody->FindInputKeyClosestToWorldLocation(Location);
		}
		TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> QueryResult =
			WaterBody->TryQueryWaterInfoClosestToWorldLocation(Location, QueryFlags, SplineInputKey);
		if (!QueryResult.HasValue() || QueryResult.GetValue().IsInExclusionVolume())
		{
			return false;
		}
		const FWaterBodyQueryResult& Query = QueryResult.GetValue();
		OutZ = Query.GetWaterSurfaceLocation().Z;
		OutNormal = FVector::UpVector;
		if (WaterBody->HasWaves())
		{
			const float WaterDepth = Query.GetWaterSurfaceDepth();
			if (UWaterWavesBase* WaterWaves = WaterBody->GetWaterWaves())
			{
				const float AttenuationFactor = WaterWaves->GetWaveAttenuationFactor(
					Query.GetWaterSurfaceLocation(), WaterDepth, WaterBody->TargetWaveMaskDepth);
				if (AttenuationFactor > 0.0f)
				{
					float RawWaveHeight = WaterWaves->GetWaveHeightAtPosition(
						Query.GetWaterSurfaceLocation(), WaterDepth, SampleTime, OutNormal);
					OutZ += RawWaveHeight * AttenuationFactor;
					if (!WaterWaves->IsA<USWRippleWaterWaves>())
					{
						if (URippleSubsystem* RippleSubsystem = GetWorld()->GetSubsystem<URippleSubsystem>())
						{
							OutZ += RippleSubsystem->GetRippleHeight(Query.GetWaterSurfaceLocation()) * AttenuationFactor;
						}
					}
				}
			}
		}
		return true;
	};

	auto ConsiderWaterBody = [&](UWaterBodyComponent* WaterBody)
	{
		float CurrentZ = -BIG_NUMBER;
		FVector CurrentNormal = FVector::UpVector;
		if (!EvaluateWaterBody(WaterBody, CurrentServerTime, CurrentZ, CurrentNormal))
		{
			return;
		}
		if (!BestSample.bIsValid || CurrentZ > BestSample.SurfaceZ)
		{
			BestSample.bIsValid = true;
			BestSample.SurfaceZ = CurrentZ;
			BestSample.SurfaceNormal = CurrentNormal;
			BestSample.WaterBody = WaterBody;
			float PreviousZ = CurrentZ;
			FVector PreviousNormal = FVector::UpVector;
			constexpr float SampleDeltaTime = 1.0f / 60.0f;
			BestSample.SurfaceVelocityZ = EvaluateWaterBody(
				WaterBody, CurrentServerTime - SampleDeltaTime, PreviousZ, PreviousNormal)
				? (CurrentZ - PreviousZ) / SampleDeltaTime
				: 0.0f;
		}
	};

	for (UWaterBodyComponent* WaterBody : OverlappingWaterBodies)
	{
		ConsiderWaterBody(WaterBody);
	}
	if (LastActiveWaterBody.IsValid() && !OverlappingWaterBodies.Contains(LastActiveWaterBody.Get()))
	{
		ConsiderWaterBody(LastActiveWaterBody.Get());
	}
	return BestSample;
}

void USwimmingComponent::CheckWaterTransitions(float DeltaSeconds)
{
	if (!OwnerCharacter || !CharacterMovement || !CapsuleComponent) return;

	bool bFeetInsideCabin = false;
	bool bCenterInsideCabin = false;
	const bool bInsideCabin = IsInsideCabinWaterCull(&bFeetInsideCabin, &bCenterInsideCabin);
	DrawCabinWaterCullDebug(bInsideCabin, bFeetInsideCabin, bCenterInsideCabin);
	TraceCabinWaterCull(bInsideCabin, bFeetInsideCabin, bCenterInsideCabin);
	const bool bLeftCabin = bWasInsideCabinWaterCull && !bInsideCabin;
	bWasInsideCabinWaterCull = bInsideCabin;
	if (bInsideCabin)
	{
		ResetSwimmingStateInsideCabin();
		return;
	}
	if (bLeftCabin)
	{
		InitializeOverlaps();
	}
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

	const FSwimWaterSurfaceSample Sample = QueryWaterSurfaceSample(ActorLocation);
	const bool bHadValidWaterBodyQuery = Sample.bIsValid;
	const bool bFeetInWater = Sample.bIsValid && FeetLocation.Z <= Sample.SurfaceZ;
	const float FeetSubmersion = Sample.bIsValid
		? (Sample.SurfaceZ - FeetLocation.Z)
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
			const float SignedDepth = Sample.bIsValid ? Sample.SurfaceZ - ActorLocation.Z : -BIG_NUMBER;
			EnterSwimMovementState(
				Sample.bIsValid && SignedDepth >= SubmergedDepthThreshold
					? ESwimMovementState::Submerged
					: ESwimMovementState::Surface,
				SignedDepth);
			bIsInShallowWater = false;
			CharacterMovement->Buoyancy = 0.f; // CMC의 기본 부력 사용 정지
			ApplySwimmingGameplayState(true);
			
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
			ApplySwimmingGameplayState(false);
			
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
			ApplySwimmingGameplayState(false);
			
			FString OwnerName = OwnerCharacter ? OwnerCharacter->GetName() : (GetOwner() ? GetOwner()->GetName() : TEXT("None"));
			FString ContextStr = (GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Server") : TEXT("Client");
			UE_LOG(LogTemp, Warning, TEXT("[%s] %s <<< Exited Swimming State (Falling) (FeetSubmersion: %.2f) >>>"), *ContextStr, *OwnerName, FeetSubmersion);
		}
	}

	if (IsCustomSwimming())
	{
		UpdateUnderwaterState(Sample);
	}
	else
	{
		bIsUnderwater = false;
		if (!bFeetInWater)
		{
			bIsInShallowWater = false;
		}
		ResetSwimMovementState();
	}
}

bool USwimmingComponent::IsInsideCabinWaterCull(
	bool* bOutFeetInside,
	bool* bOutCenterInside) const
{
	if (bOutFeetInside)
	{
		*bOutFeetInside = false;
	}
	if (bOutCenterInside)
	{
		*bOutCenterInside = false;
	}
	if (!CapsuleComponent)
	{
		return false;
	}

	const FVector Center = CapsuleComponent->GetComponentLocation();
	const FVector Feet = Center - CapsuleComponent->GetUpVector()
		* CapsuleComponent->GetScaledCapsuleHalfHeight();
	UWorld* World = GetWorld();
	const bool bFeetInside = USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(World, Feet);
	const bool bCenterInside = USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(World, Center);
	if (bOutFeetInside)
	{
		*bOutFeetInside = bFeetInside;
	}
	if (bOutCenterInside)
	{
		*bOutCenterInside = bCenterInside;
	}
	return bFeetInside || bCenterInside;
}

void USwimmingComponent::DrawCabinWaterCullDebug(
	bool bInsideCabin,
	bool bFeetInside,
	bool bCenterInside) const
{
	if (CVarShowCabinSwimCullDebug.GetValueOnGameThread() <= 0
		|| !GEngine
		|| !OwnerCharacter
		|| !OwnerCharacter->IsLocallyControlled()
		|| !CapsuleComponent)
	{
		return;
	}

	const float CapsuleHalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
	const FVector FeetLocation = CapsuleComponent->GetComponentLocation()
		- CapsuleComponent->GetUpVector() * CapsuleHalfHeight;
	const FSwimWaterSurfaceSample Sample = QueryWaterSurfaceSample(CapsuleComponent->GetComponentLocation());
	const bool bHadValidWaterQuery = Sample.bIsValid;
	const bool bFeetInWater = Sample.bIsValid && FeetLocation.Z <= Sample.SurfaceZ;
	const float FeetSubmersion = Sample.bIsValid
		? Sample.SurfaceZ - FeetLocation.Z
		: -100000.0f;
	const float SwimEntryDepth = CapsuleHalfHeight * 2.0f
		* SwimEntryCapsuleSubmersionRatio;
	const bool bWouldEnterSwimming = bFeetInWater && FeetSubmersion >= SwimEntryDepth;
	const bool bBlockingSwimming = bInsideCabin && bWouldEnterSwimming;

	const TCHAR* WaterState = !bHadValidWaterQuery
		? TEXT("NO QUERY")
		: (bFeetInWater ? TEXT("IN WATER") : TEXT("DRY"));
	const FString DebugText = FString::Printf(
		TEXT("[Cabin Swim Cull] Cabin=%s (Feet:%s Center:%s) | Water=%s Depth=%.1f/%.1f | Blocking=%s | Swim=%s Mode=%d:%d VelZ=%.1f"),
		bInsideCabin ? TEXT("INSIDE") : TEXT("OUTSIDE"),
		bFeetInside ? TEXT("IN") : TEXT("OUT"),
		bCenterInside ? TEXT("IN") : TEXT("OUT"),
		WaterState,
		bHadValidWaterQuery ? FeetSubmersion : 0.0f,
		SwimEntryDepth,
		bBlockingSwimming ? TEXT("YES") : TEXT("NO"),
		IsCustomSwimming() ? TEXT("ON") : TEXT("OFF"),
		int32(CharacterMovement->MovementMode),
		int32(CharacterMovement->CustomMovementMode),
		CharacterMovement->Velocity.Z);
	const FColor DebugColor = bBlockingSwimming
		? FColor::Red
		: (bInsideCabin ? FColor::Yellow : FColor::Green);
	const uint64 MessageKey = 0xCAB10000ull + uint64(GetUniqueID());
	GEngine->AddOnScreenDebugMessage(MessageKey, 0.15f, DebugColor, DebugText);
}

void USwimmingComponent::TraceCabinWaterCull(
	bool bInsideCabin,
	bool bFeetInside,
	bool bCenterInside)
{
	if (CVarCabinSwimTrace.GetValueOnGameThread() <= 0
		|| !OwnerCharacter
		|| !OwnerCharacter->IsLocallyControlled()
		|| !CharacterMovement
		|| !CapsuleComponent
		|| !GetWorld())
	{
		return;
	}

	const float WorldTime = GetWorld()->GetTimeSeconds();
	if (LastCabinSwimTraceTime >= 0.0f && WorldTime - LastCabinSwimTraceTime < 0.05f)
	{
		return;
	}
	LastCabinSwimTraceTime = WorldTime;

	const FVector Up = CapsuleComponent->GetUpVector();
	const float CapsuleHalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
	const FVector Center = CapsuleComponent->GetComponentLocation();
	const FVector Feet = Center - Up * CapsuleHalfHeight;
	auto SampleCabin = [this](const FVector& Position)
	{
		return USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(GetWorld(), Position);
	};

	const FSwimWaterSurfaceSample Sample = QueryWaterSurfaceSample(Center);
	const bool bHadValidWaterQuery = Sample.bIsValid;
	const bool bFeetInWater = Sample.bIsValid && Feet.Z <= Sample.SurfaceZ;
	const float WaterHeight = Sample.SurfaceZ;
	const float FeetSubmersion = Sample.bIsValid ? Sample.SurfaceZ - Feet.Z : -100000.0f;
	const float SwimEntryDepth = CapsuleHalfHeight * 2.0f * SwimEntryCapsuleSubmersionRatio;
	const bool bWouldEnterSwimming = bFeetInWater && FeetSubmersion >= SwimEntryDepth;
	UPrimitiveComponent* MovementBase = CharacterMovement->GetMovementBase();
	const FTransform BaseTransform = MovementBase
		? MovementBase->GetComponentTransform()
		: FTransform::Identity;
	const FVector BaseLocalCenter = MovementBase
		? BaseTransform.InverseTransformPosition(Center)
		: Center;
	const FRotator BaseRotation = BaseTransform.Rotator();
	const USkeletalMeshComponent* CharacterMesh = OwnerCharacter->GetMesh();
	const FVector LeftFoot = CharacterMesh
		? CharacterMesh->GetSocketLocation(TEXT("foot_l"))
		: Center;
	const FVector RightFoot = CharacterMesh
		? CharacterMesh->GetSocketLocation(TEXT("foot_r"))
		: Center;
	const FVector LeftFootBaseLocal = MovementBase
		? BaseTransform.InverseTransformPosition(LeftFoot)
		: LeftFoot;
	const FVector RightFootBaseLocal = MovementBase
		? BaseTransform.InverseTransformPosition(RightFoot)
		: RightFoot;

	FCollisionQueryParams FootTraceParams(SCENE_QUERY_STAT(CabinSwimFootTrace), true);
	FootTraceParams.AddIgnoredActor(OwnerCharacter);
	FHitResult LeftFootHit;
	const bool bLeftFootHit = GetWorld()->SweepSingleByChannel(
		LeftFootHit,
		LeftFoot + FVector::UpVector * 75.0f,
		LeftFoot - FVector::UpVector * 100.0f,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(5.0f),
		FootTraceParams);

	UE_LOG(LogTemp, Warning,
		TEXT("[CabinSwimTrace] T=%.3f ActorZ=%.2f FeetZ=%.2f Cabin=%d Samples[-20,0,+20,+50,C]=%d%d%d%d%d Water[Valid,Wet,Z,Depth,Entry]=%d,%d,%.2f,%.2f,%.2f WouldEnter=%d Block=%d Mode=%d:%d VelZ=%.2f Floor=%d Base=%s BaseZ=%.2f BaseRot[P,R]=%.2f,%.2f BaseLocal[X,Y,Z]=%.2f,%.2f,%.2f BoneLocalZ[L,R]=%.2f,%.2f FootHit=%d,%s,%s,%.2f"),
		WorldTime,
		Center.Z,
		Feet.Z,
		bInsideCabin,
		SampleCabin(Feet - Up * 20.0f),
		bFeetInside,
		SampleCabin(Feet + Up * 20.0f),
		SampleCabin(Feet + Up * 50.0f),
		bCenterInside,
		bHadValidWaterQuery,
		bFeetInWater,
		WaterHeight,
		bHadValidWaterQuery ? FeetSubmersion : 0.0f,
		SwimEntryDepth,
		bWouldEnterSwimming,
		bInsideCabin && bWouldEnterSwimming,
		int32(CharacterMovement->MovementMode),
		int32(CharacterMovement->CustomMovementMode),
		CharacterMovement->Velocity.Z,
		CharacterMovement->CurrentFloor.IsWalkableFloor(),
		*GetNameSafe(MovementBase),
		MovementBase ? MovementBase->GetComponentLocation().Z : 0.0f,
		BaseRotation.Pitch,
		BaseRotation.Roll,
		BaseLocalCenter.X,
		BaseLocalCenter.Y,
		BaseLocalCenter.Z,
		LeftFootBaseLocal.Z,
		RightFootBaseLocal.Z,
		bLeftFootHit,
		*GetNameSafe(LeftFootHit.GetComponent()),
		LeftFootHit.GetComponent() ? *LeftFootHit.GetComponent()->GetCollisionProfileName().ToString() : TEXT("None"),
		bLeftFootHit && MovementBase
			? BaseTransform.InverseTransformPosition(LeftFootHit.ImpactPoint).Z
			: 0.0f);
}

void USwimmingComponent::ResetSwimmingStateInsideCabin()
{
	bIsInShallowWater = false;
	bIsUnderwater = false;
	WaterQueryFailureElapsed = 0.0f;
	ResetSwimMovementState();

	if (IsCustomSwimming() && CharacterMovement)
	{
		CharacterMovement->SetMovementMode(
			CharacterMovement->CurrentFloor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
		// Custom swimming writes buoyant acceleration directly into Velocity.Z.
		// Do not carry that upward impulse into dry cabin movement.
		CharacterMovement->Velocity.Z = FMath::Min(CharacterMovement->Velocity.Z, 0.0f);
	}
	ApplySwimmingGameplayState(false);
}

void USwimmingComponent::UpdateSwimmingMovement(float DeltaTime)
{
	if (!OwnerCharacter || !CharacterMovement)
	{
		return;
	}

	const FVector ActorLocation = OwnerCharacter->GetActorLocation();
	const FSwimWaterSurfaceSample Sample = QueryWaterSurfaceSample(ActorLocation);
	const float EffectiveVerticalInput = GetEffectiveVerticalSwimInput();
	const float InputVerticalAcceleration = EffectiveVerticalInput * VerticalSwimAcceleration;
	const bool bHasVerticalInput = HasVerticalSwimInput();
	const bool bUseCameraDirectedMovement = ShouldUseCameraDirectedUnderwaterMovement();

	if (MovementState == ESwimMovementState::Surface)
	{
		if (Sample.bIsValid)
		{
			const float TargetActorZ = Sample.SurfaceZ - SurfaceTargetDepth;
			const float FollowAcceleration = ComputeSurfaceFollowAcceleration(
				TargetActorZ - ActorLocation.Z,
				Sample.SurfaceVelocityZ - CharacterMovement->Velocity.Z,
				SurfaceFollowFrequencyHz,
				SurfaceFollowDampingRatio,
				MaxSurfaceFollowAcceleration);
			CharacterMovement->Velocity.Z = FMath::Clamp(
				CharacterMovement->Velocity.Z + FollowAcceleration * DeltaTime,
				-MaxSurfaceFollowSpeed,
				MaxSurfaceFollowSpeed);
		}
		else
		{
			CharacterMovement->Velocity.Z = FMath::FInterpTo(
				CharacterMovement->Velocity.Z, 0.0f, DeltaTime, SubmergedVerticalDamping);
		}
	}
	else if (bHasVerticalInput)
	{
		CharacterMovement->Velocity.Z = FMath::Clamp(
			CharacterMovement->Velocity.Z + InputVerticalAcceleration * DeltaTime,
			-MaxVerticalSwimSpeed,
			MaxVerticalSwimSpeed);
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
		const FVector InputDirection = IsTransitionState() || bHasVerticalInput
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

	UpdateSwimState(DeltaTime, Sample, SweepHit.IsValidBlockingHit());
	UpdateUnderwaterState(Sample);

	if (CVarSwimTransitionDebug.GetValueOnGameThread() != 0
		&& GetWorld()
		&& GetWorld()->GetTimeSeconds() - LastLoggedTime >= 0.25f)
	{
		LastLoggedTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogTemp, Warning,
			TEXT("[SwimTransition] Pawn=%s Role=%d State=%d Raw=%.1f Effective=%.1f Valid=%d SurfaceZ=%.1f ActorZ=%.1f Depth=%.1f TargetDepth=%.1f VelZ=%.1f SurfaceVelZ=%.1f DiveT=%.2f SurfaceT=%.2f StallT=%.2f Blocking=%d"),
			*GetNameSafe(OwnerCharacter),
			static_cast<int32>(OwnerCharacter->GetLocalRole()),
			static_cast<int32>(MovementState),
			RawVerticalSwimInput,
			EffectiveVerticalInput,
			Sample.bIsValid ? 1 : 0,
			Sample.SurfaceZ,
			OwnerCharacter->GetActorLocation().Z,
			Sample.bIsValid ? Sample.SurfaceZ - OwnerCharacter->GetActorLocation().Z : 0.0f,
			SurfaceTargetDepth,
			CharacterMovement->Velocity.Z,
			Sample.SurfaceVelocityZ,
			DiveTransitionElapsed,
			SurfaceTransitionElapsed,
			SurfaceTransitionStallElapsed,
			SweepHit.IsValidBlockingHit() ? 1 : 0);
	}
}

bool USwimmingComponent::RequestDiveTransition()
{
	if (!IsCustomSwimming() || MovementState != ESwimMovementState::Surface)
	{
		return false;
	}
	EnterSwimMovementState(ESwimMovementState::DiveTransition);
	if (CharacterMovement)
	{
		CharacterMovement->ConsumeInputVector();
	}
	return true;
}

void USwimmingComponent::EnterSwimMovementState(ESwimMovementState NewState, float InitialDepth)

{
	if (NewState == ESwimMovementState::SurfaceTransition)
	{
		RawVerticalSwimInput = 0.0f;
		bDiveInputSuppressedUntilRelease |= bRawDiveInputHeld;
		bAscendInputSuppressedUntilRelease |= bRawAscendInputHeld;
	}
	MovementState = NewState;
	DiveTransitionElapsed = 0.0f;
	SurfaceTransitionElapsed = 0.0f;
	SurfaceTransitionStallElapsed = 0.0f;
	SurfaceTransitionEntryHoldElapsed = 0.0f;
	LastSurfaceTransitionProgressDepth = InitialDepth;
	RefreshEffectiveVerticalInput();
}

void USwimmingComponent::ResetSwimMovementState()

{
	RawVerticalSwimInput = 0.0f;
	bRawDiveInputHeld = false;
	bRawAscendInputHeld = false;
	bDiveInputSuppressedUntilRelease = false;
	bAscendInputSuppressedUntilRelease = false;
	EnterSwimMovementState(ESwimMovementState::Surface);
}

void USwimmingComponent::UpdateSwimState(
	float DeltaTime,
	const FSwimWaterSurfaceSample& Sample,
	bool bBlockingHit)

{
	if (!Sample.bIsValid || !OwnerCharacter || !CharacterMovement)
	{
		return;
	}
	const float SafeTargetDepth = FMath::Max(SurfaceTargetDepth, 0.0f);
	const float SafeTolerance = FMath::Max(SurfaceTransitionCompletionTolerance, 0.0f);
	const float SafeThreshold = FMath::Max(SubmergedDepthThreshold, SafeTargetDepth + SafeTolerance + UE_SMALL_NUMBER);
	const float SafeDiveDuration = FMath::Max(DiveTransitionDuration, 0.0f);
	const float SafeEntryHold = FMath::Max(SurfaceTransitionEntryHoldTime, UE_SMALL_NUMBER);
	const float SafeMaxDuration = FMath::Max(SurfaceTransitionMaxDuration, UE_SMALL_NUMBER);
	const float SafeStallTimeout = FMath::Max(SurfaceTransitionStallTimeout, UE_SMALL_NUMBER);
	const float SignedDepth = Sample.SurfaceZ - OwnerCharacter->GetActorLocation().Z;

	if (!bLoggedInvalidStateTuning
		&& (SubmergedDepthThreshold <= SurfaceTargetDepth + SurfaceTransitionCompletionTolerance
			|| DiveTransitionDuration < 0.0f
			|| SurfaceTransitionEntryHoldTime <= 0.0f
			|| SurfaceTransitionMaxDuration <= 0.0f
			|| SurfaceTransitionStallTimeout <= 0.0f
			|| SurfaceFollowFrequencyHz <= 0.0f
			|| SurfaceFollowDampingRatio < 0.0f))
	{
		bLoggedInvalidStateTuning = true;
		UE_LOG(LogTemp, Warning, TEXT("Invalid swimming state tuning on %s; runtime-safe clamps are active."), *GetNameSafe(this));
	}

	switch (MovementState)
	{
	case ESwimMovementState::DiveTransition:
		DiveTransitionElapsed += DeltaTime;
		if (DiveTransitionElapsed >= SafeDiveDuration)
		{
			EnterSwimMovementState(
				SignedDepth >= SafeThreshold ? ESwimMovementState::Submerged : ESwimMovementState::SurfaceTransition,
				SignedDepth);
		}
		break;
	case ESwimMovementState::Submerged:
		if (SignedDepth < SafeThreshold)
		{
			SurfaceTransitionEntryHoldElapsed += DeltaTime;
			if (SurfaceTransitionEntryHoldElapsed >= SafeEntryHold)
			{
				EnterSwimMovementState(ESwimMovementState::SurfaceTransition, SignedDepth);
			}
		}
		else
		{
			SurfaceTransitionEntryHoldElapsed = 0.0f;
		}
		break;
	case ESwimMovementState::SurfaceTransition:
		SurfaceTransitionElapsed += DeltaTime;
		if (FMath::Abs(SignedDepth - SafeTargetDepth) <= SafeTolerance || SignedDepth <= SafeTargetDepth)
		{
			CharacterMovement->Velocity.Z = FMath::Clamp(
				Sample.SurfaceVelocityZ, -MaxSurfaceFollowSpeed, MaxSurfaceFollowSpeed);
			EnterSwimMovementState(ESwimMovementState::Surface, SignedDepth);
			break;
		}
		if (LastSurfaceTransitionProgressDepth - SignedDepth >= 5.0f)
		{
			LastSurfaceTransitionProgressDepth = SignedDepth;
			SurfaceTransitionStallElapsed = 0.0f;
		}
		else if (bBlockingHit)
		{
			SurfaceTransitionStallElapsed += DeltaTime;
		}
		if (SurfaceTransitionElapsed >= SafeMaxDuration || SurfaceTransitionStallElapsed >= SafeStallTimeout)
		{
			EnterSwimMovementState(ESwimMovementState::Submerged, SignedDepth);
		}
		break;
	default:
		break;
	}
	const float EffectiveInput = GetEffectiveVerticalSwimInput();
	bDiveInputHeld = EffectiveInput < -KINDA_SMALL_NUMBER;
	bAscendInputHeld = EffectiveInput > KINDA_SMALL_NUMBER;
}

void USwimmingComponent::UpdateUnderwaterState(const FSwimWaterSurfaceSample& Sample)

{
	if (!OwnerCharacter || !CapsuleComponent || !IsCustomSwimming() || !Sample.bIsValid)
	{
		bIsUnderwater = false;
		return;
	}

	const float HeadZ = OwnerCharacter->GetActorLocation().Z
		+ CapsuleComponent->GetUnscaledCapsuleHalfHeight() - 15.0f;
	const float WaterHeightRelativeToHead = Sample.SurfaceZ - HeadZ;

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

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSwimmingSurfaceFollowTest,
	"ArtisticSW.Swimming.SurfaceFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingSurfaceFollowTest::RunTest(const FString& Parameters)
{
	const float BelowTarget = ComputeSurfaceFollowAcceleration(25.0f, 0.0f, 2.0f, 1.0f, 4000.0f);
	const float AboveTarget = ComputeSurfaceFollowAcceleration(-25.0f, 0.0f, 2.0f, 1.0f, 4000.0f);
	const float AtTarget = ComputeSurfaceFollowAcceleration(0.0f, 0.0f, 2.0f, 1.0f, 4000.0f);
	TestTrue(TEXT("Below target accelerates upward"), BelowTarget > 0.0f);
	TestTrue(TEXT("Above target accelerates downward"), AboveTarget < 0.0f);
	TestTrue(TEXT("Matching position and velocity produces zero acceleration"), FMath::IsNearlyZero(AtTarget));
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
