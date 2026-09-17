#include "SWCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Ship.h"
#include "SwimmingComponent.h"

namespace
{
	constexpr double MaxSurfaceWaveTimeAgeSeconds = 2.0;
	constexpr double MaxSurfaceWaveTimeLeadSeconds = 0.25;
	constexpr double SurfaceWaveTimeDeltaToleranceSeconds = 0.25;
	constexpr double SurfaceWaveTimeBackwardToleranceSeconds = 0.05;

	class FSavedMove_SWCharacter final : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;

		uint8 bSavedSwimDive : 1;
		uint8 bSavedSwimAscend : 1;
		FSwimPredictionState SavedSwimState;
		bool bHasSurfaceWaveServerTime = false;
		double SurfaceWaveServerTimeSeconds = 0.0;

		FSavedMove_SWCharacter()
			: bSavedSwimDive(false)
			, bSavedSwimAscend(false)
		{
		}

		virtual void Clear() override
		{
			Super::Clear();
			bSavedSwimDive = false;
			bSavedSwimAscend = false;
			SavedSwimState = FSwimPredictionState();
			bHasSurfaceWaveServerTime = false;
			SurfaceWaveServerTimeSeconds = 0.0;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Result = Super::GetCompressedFlags();
			if (bSavedSwimDive)
			{
				Result |= FLAG_Custom_0;
			}
			if (bSavedSwimAscend)
			{
				Result |= FLAG_Custom_1;
			}
			return Result;
		}

		virtual bool CanCombineWith(
			const FSavedMovePtr& NewMove,
			ACharacter* InCharacter,
			float MaxDelta) const override
		{
			const FSavedMove_SWCharacter* NewSWMove =
				static_cast<const FSavedMove_SWCharacter*>(NewMove.Get());
			if (bSavedSwimDive != NewSWMove->bSavedSwimDive
				|| bSavedSwimAscend != NewSWMove->bSavedSwimAscend
				|| SavedSwimState.MovementState != NewSWMove->SavedSwimState.MovementState
				|| SavedSwimState.bRawDiveInputHeld != NewSWMove->SavedSwimState.bRawDiveInputHeld
				|| SavedSwimState.bRawAscendInputHeld != NewSWMove->SavedSwimState.bRawAscendInputHeld
				|| SavedSwimState.bDiveInputSuppressedUntilRelease != NewSWMove->SavedSwimState.bDiveInputSuppressedUntilRelease
				|| SavedSwimState.bAscendInputSuppressedUntilRelease != NewSWMove->SavedSwimState.bAscendInputSuppressedUntilRelease
				|| FMath::Abs(SavedSwimState.SurfaceTransitionElapsed - NewSWMove->SavedSwimState.SurfaceTransitionElapsed) > MaxDelta
				|| FMath::Abs(SavedSwimState.SurfaceTransitionStallElapsed - NewSWMove->SavedSwimState.SurfaceTransitionStallElapsed) > MaxDelta
				|| FMath::Abs(SavedSwimState.SurfaceTransitionEntryHoldElapsed - NewSWMove->SavedSwimState.SurfaceTransitionEntryHoldElapsed) > MaxDelta
				|| !FMath::IsNearlyEqual(SavedSwimState.LastSurfaceTransitionProgressDepth, NewSWMove->SavedSwimState.LastSurfaceTransitionProgressDepth, 5.0f))
			{
				return false;
			}
			if (bHasSurfaceWaveServerTime != NewSWMove->bHasSurfaceWaveServerTime
				|| (bHasSurfaceWaveServerTime
					&& SurfaceWaveServerTimeSeconds != NewSWMove->SurfaceWaveServerTimeSeconds))
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void SetMoveFor(
			ACharacter* Character,
			float InDeltaTime,
			const FVector& NewAcceleration,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(Character, InDeltaTime, NewAcceleration, ClientData);
			if (USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				const float VerticalInput = Movement->GetSwimmingVerticalInput();
				bSavedSwimDive = VerticalInput < -KINDA_SMALL_NUMBER;
				bSavedSwimAscend = VerticalInput > KINDA_SMALL_NUMBER;
				SavedSwimState = Movement->GetSwimmingPredictionState();
				double CapturedTime = 0.0;
				bHasSurfaceWaveServerTime = Movement->ShouldCaptureSurfaceWaveTime()
					&& Movement->CaptureCurrentSurfaceWaveServerTime(CapturedTime);
				SurfaceWaveServerTimeSeconds = bHasSurfaceWaveServerTime ? CapturedTime : 0.0;
				if (bHasSurfaceWaveServerTime)
				{
					Movement->SetActiveSurfaceWaveServerTime(SurfaceWaveServerTimeSeconds);
				}
				else
				{
					Movement->ClearActiveSurfaceWaveServerTime();
				}
			}
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			if (USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				Movement->RestoreSavedSwimmingState(SavedSwimState);
				if (bHasSurfaceWaveServerTime)
				{
					Movement->SetActiveSurfaceWaveServerTime(SurfaceWaveServerTimeSeconds);
				}
				else
				{
					Movement->ClearActiveSurfaceWaveServerTime();
				}
			}
		}

		virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override
		{
			Super::PostUpdate(Character, PostUpdateMode);
			if (USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				Movement->ClearActiveSurfaceWaveServerTime();
			}
		}
	};

	class FNetworkPredictionData_Client_SWCharacter final
		: public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FNetworkPredictionData_Client_SWCharacter(
			const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return MakeShared<FSavedMove_SWCharacter>();
		}
	};
}

void FCharacterNetworkMoveData_SWCharacter::ClientFillNetworkMoveData(
	const FSavedMove_Character& ClientMove,
	ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	const FSavedMove_SWCharacter& SWMove = static_cast<const FSavedMove_SWCharacter&>(ClientMove);
	bHasSurfaceWaveServerTime = SWMove.bHasSurfaceWaveServerTime;
	SurfaceWaveServerTimeSeconds = bHasSurfaceWaveServerTime ? SWMove.SurfaceWaveServerTimeSeconds : 0.0;
}

bool FCharacterNetworkMoveData_SWCharacter::Serialize(
	UCharacterMovementComponent& CharacterMovement,
	FArchive& Ar,
	UPackageMap* PackageMap,
	ENetworkMoveType MoveType)
{
	if (!FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
	{
		return false;
	}
	Ar.SerializeBits(&bHasSurfaceWaveServerTime, 1);
	if (bHasSurfaceWaveServerTime)
	{
		Ar << SurfaceWaveServerTimeSeconds;
		if (Ar.IsLoading()
			&& (!FMath::IsFinite(SurfaceWaveServerTimeSeconds) || SurfaceWaveServerTimeSeconds < 0.0))
		{
			SurfaceWaveServerTimeSeconds = 0.0;
			Ar.SetError();
		}
	}
	else
	{
		SurfaceWaveServerTimeSeconds = 0.0;
	}
	return !Ar.IsError();
}

FCharacterNetworkMoveDataContainer_SWCharacter::FCharacterNetworkMoveDataContainer_SWCharacter()
{
	NewMoveData = &MoveData[0];
	PendingMoveData = &MoveData[1];
	OldMoveData = &MoveData[2];
}

USWCharacterMovementComponent::USWCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetNetworkMoveDataContainer(SWMoveDataContainer);
	ProcessRootMotionPostConvertToWorld.BindUObject(
		this, &USWCharacterMovementComponent::RedirectHitReactionRootMotion);
}

void USWCharacterMovementComponent::BeginHitReactionRootMotion(const FVector& WorldDirection)
{
	HitReactionRootMotionDirection = FVector(WorldDirection.X, WorldDirection.Y, 0.0f).GetSafeNormal();
	bRedirectHitReactionRootMotion = !HitReactionRootMotionDirection.IsNearlyZero();
}

void USWCharacterMovementComponent::EndHitReactionRootMotion()
{
	bRedirectHitReactionRootMotion = false;
	HitReactionRootMotionDirection = FVector::ZeroVector;
}

FTransform USWCharacterMovementComponent::RedirectRootMotionTranslation(
	const FTransform& WorldRootMotion,
	const FVector& WorldDirection)
{
	const FVector HorizontalDirection = FVector(WorldDirection.X, WorldDirection.Y, 0.0f).GetSafeNormal();
	if (HorizontalDirection.IsNearlyZero())
	{
		return WorldRootMotion;
	}

	FTransform RedirectedRootMotion = WorldRootMotion;
	const FVector AuthoredTranslation = WorldRootMotion.GetTranslation();
	const float HorizontalDistance = AuthoredTranslation.Size2D();
	if (HorizontalDistance > KINDA_SMALL_NUMBER)
	{
		FVector RedirectedTranslation = HorizontalDirection * HorizontalDistance;
		RedirectedTranslation.Z = AuthoredTranslation.Z;
		RedirectedRootMotion.SetTranslation(RedirectedTranslation);
	}
	return RedirectedRootMotion;
}

FTransform USWCharacterMovementComponent::RedirectHitReactionRootMotion(
	const FTransform& WorldRootMotion,
	UCharacterMovementComponent* MovementComponent,
	float DeltaSeconds) const
{
	if (!bRedirectHitReactionRootMotion || HitReactionRootMotionDirection.IsNearlyZero())
	{
		return WorldRootMotion;
	}

	return RedirectRootMotionTranslation(WorldRootMotion, HitReactionRootMotionDirection);
}

void USWCharacterMovementComponent::SetSwimmingVerticalInput(float InVerticalInput)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->SetVerticalSwimInput(InVerticalInput);
		}
	}
}

float USWCharacterMovementComponent::GetSwimmingVerticalInput() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp =
			CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetVerticalSwimInput();
		}
	}
	return 0.0f;
}

ESwimMovementState USWCharacterMovementComponent::GetSwimmingMovementState() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp =
			CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetMovementState();
		}
	}
	return ESwimMovementState::Surface;
}

void USWCharacterMovementComponent::SetSwimmingVerticalInput(bool bDiveHeld, bool bAscendHeld)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->SetRawVerticalSwimInput(bDiveHeld, bAscendHeld);
		}
	}
}

FSwimPredictionState USWCharacterMovementComponent::GetSwimmingPredictionState() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetPredictionState();
		}
	}
	return FSwimPredictionState();
}

void USWCharacterMovementComponent::RestoreSavedSwimmingState(
	const FSwimPredictionState& InState)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->RestorePredictedSwimState(InState);
		}
	}
}

bool USWCharacterMovementComponent::ShouldCaptureSurfaceWaveTime() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->NeedsDeterministicWaveTime();
		}
	}
	return false;
}

bool USWCharacterMovementComponent::CaptureCurrentSurfaceWaveServerTime(double& OutServerTime) const
{
	OutServerTime = 0.0;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return false;
	}
	const double ServerTime = static_cast<double>(GameState->GetServerWorldTimeSeconds());
	if (!FMath::IsFinite(ServerTime) || ServerTime < 0.0)
	{
		return false;
	}
	OutServerTime = ServerTime;
	return true;
}

void USWCharacterMovementComponent::SetActiveSurfaceWaveServerTime(double ServerTimeSeconds)
{
	if (!FMath::IsFinite(ServerTimeSeconds) || ServerTimeSeconds < 0.0)
	{
		ClearActiveSurfaceWaveServerTime();
		return;
	}
	bHasActiveSurfaceWaveServerTime = true;
	ActiveSurfaceWaveServerTimeSeconds = ServerTimeSeconds;
}

void USWCharacterMovementComponent::ClearActiveSurfaceWaveServerTime()
{
	bHasActiveSurfaceWaveServerTime = false;
	ActiveSurfaceWaveServerTimeSeconds = 0.0;
}

bool USWCharacterMovementComponent::TryGetActiveSurfaceWaveServerTime(double& OutServerTime) const
{
	OutServerTime = bHasActiveSurfaceWaveServerTime ? ActiveSurfaceWaveServerTimeSeconds : 0.0;
	return bHasActiveSurfaceWaveServerTime;
}

void USWCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	if (CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming))
	{
		if (ACharacter* CharOwner = CharacterOwner)
		{
			if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
			{
				SwimComp->UpdateSwimmingMovement(DeltaTime);
			}
		}
	}
}

void USWCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	const bool bDive = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	const bool bAscend = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	SetSwimmingVerticalInput(bDive, bAscend);
}

FNetworkPredictionData_Client* USWCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		USWCharacterMovementComponent* MutableThis =
			const_cast<USWCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData =
			new FNetworkPredictionData_Client_SWCharacter(*this);
	}
	return ClientPredictionData;
}

bool USWCharacterMovementComponent::ValidateSurfaceWaveServerTime(
	double TransmittedTime,
	float ClientTimeStamp,
	bool bIsOldMove,
	double ServerNow) const
{
	if (!FMath::IsFinite(TransmittedTime) || TransmittedTime < 0.0
		|| TransmittedTime < ServerNow - MaxSurfaceWaveTimeAgeSeconds
		|| TransmittedTime > ServerNow + MaxSurfaceWaveTimeLeadSeconds)
	{
		return false;
	}
	if (bIsOldMove || !bHasAcceptedSurfaceWaveTimeAnchor)
	{
		return true;
	}
	const double WaveDelta = TransmittedTime - LastAcceptedSurfaceWaveServerTimeSeconds;
	const double MoveDelta = static_cast<double>(ClientTimeStamp - LastAcceptedSurfaceWaveClientTimeStamp);
	return TransmittedTime >= LastAcceptedSurfaceWaveServerTimeSeconds - SurfaceWaveTimeBackwardToleranceSeconds
		&& FMath::Abs(WaveDelta - MoveDelta) <= SurfaceWaveTimeDeltaToleranceSeconds;
}

void USWCharacterMovementComponent::MoveAutonomous(
	float ClientTimeStamp,
	float DeltaTime,
	uint8 CompressedFlags,
	const FVector& NewAccel)
{
	bForceSurfaceWaveCorrectionForCurrentMove = false;
	ClearActiveSurfaceWaveServerTime();
	ON_SCOPE_EXIT
	{
		ClearActiveSurfaceWaveServerTime();
	};

	const FCharacterNetworkMoveData* CurrentData = GetCurrentNetworkMoveData();
	const FCharacterNetworkMoveData_SWCharacter* SWData =
		static_cast<const FCharacterNetworkMoveData_SWCharacter*>(CurrentData);
	if (SWData && SWData->bHasSurfaceWaveServerTime)
	{
		const bool bAuthority = CharacterOwner && CharacterOwner->HasAuthority();
		if (!bAuthority)
		{
			SetActiveSurfaceWaveServerTime(SWData->SurfaceWaveServerTimeSeconds);
		}
		else if (const UWorld* World = GetWorld())
		{
			if (const AGameStateBase* GameState = World->GetGameState())
			{
				const bool bIsOldMove = SWMoveDataContainer.IsOldMoveData(CurrentData);
				const double ServerNow = static_cast<double>(GameState->GetServerWorldTimeSeconds());
				if (ValidateSurfaceWaveServerTime(
					SWData->SurfaceWaveServerTimeSeconds, ClientTimeStamp, bIsOldMove, ServerNow))
				{
					SetActiveSurfaceWaveServerTime(SWData->SurfaceWaveServerTimeSeconds);
					if (!bIsOldMove)
					{
						bHasAcceptedSurfaceWaveTimeAnchor = true;
						LastAcceptedSurfaceWaveServerTimeSeconds = SWData->SurfaceWaveServerTimeSeconds;
						LastAcceptedSurfaceWaveClientTimeStamp = ClientTimeStamp;
					}
				}
				else if (SWMoveDataContainer.IsNewMoveData(CurrentData))
				{
					bForceSurfaceWaveCorrectionForCurrentMove = true;
				}
			}
		}
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void USWCharacterMovementComponent::OnClientTimeStampResetDetected()
{
	Super::OnClientTimeStampResetDetected();
	if (bHasAcceptedSurfaceWaveTimeAnchor)
	{
		LastAcceptedSurfaceWaveClientTimeStamp -= MinTimeBetweenTimeStampResets;
	}
}

void USWCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (MovementMode == MOVE_Walking)
	{
		UPrimitiveComponent* Base = CharacterOwner ? CharacterOwner->GetMovementBase() : nullptr;
		AShip* Ship = Base ? Cast<AShip>(Base->GetOwner()) : nullptr;
		LastStandingShip = Ship;
	}
	else if (MovementMode == MOVE_Swimming || CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming))
	{
		LastStandingShip = nullptr;
	}

	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->CheckWaterTransitions(DeltaSeconds);
		}
	}
}

bool USWCharacterMovementComponent::ServerExceedsAllowablePositionError(
	float ClientTimeStamp,
	float DeltaTime,
	const FVector& Accel,
	const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation,
	UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName,
	uint8 ClientMovementMode)
{
	if (bForceSurfaceWaveCorrectionForCurrentMove)
	{
		return true;
	}
	const bool bExceedsDefaultTolerance = Super::ServerExceedsAllowablePositionError(
		ClientTimeStamp,
		DeltaTime,
		Accel,
		ClientWorldLocation,
		RelativeClientLocation,
		ClientMovementBase,
		ClientBaseBoneName,
		ClientMovementMode);

	if (!bExceedsDefaultTolerance)
	{
		return false;
	}

	// 배 위에서 점프/낙하 중(MOVE_Falling)일 때, 네트워크 물리 시뮬레이션 지연으로 인한 월드 좌표 오차를 수용하여 클라이언트 보정 스냅 방지
	if (LastStandingShip.IsValid() && (MovementMode == MOVE_Falling || ClientMovementMode == MOVE_Falling))
	{
		return false;
	}

	float RelativeError = 0.0f;
	if (CanUseShipBasedClientPosition(
		RelativeClientLocation,
		ClientMovementBase,
		ClientBaseBoneName,
		ClientMovementMode,
		RelativeError))
	{
		return false;
	}

	return true;
}

bool USWCharacterMovementComponent::ServerShouldUseAuthoritativePosition(
	float ClientTimeStamp,
	float DeltaTime,
	const FVector& Accel,
	const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation,
	UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName,
	uint8 ClientMovementMode)
{
	float RelativeError = 0.0f;
	if (CanUseShipBasedClientPosition(
		RelativeClientLocation,
		ClientMovementBase,
		ClientBaseBoneName,
		ClientMovementMode,
		RelativeError))
	{
		// Reconstructing ClientWorldLocation from the server's current ship
		// transform maps the client's predicted base-relative result onto the
		// authoritative ship without requiring both game threads to sample the
		// async physics body on the same render frame.
		return true;
	}

	return Super::ServerShouldUseAuthoritativePosition(
		ClientTimeStamp,
		DeltaTime,
		Accel,
		ClientWorldLocation,
		RelativeClientLocation,
		ClientMovementBase,
		ClientBaseBoneName,
		ClientMovementMode);
}

bool USWCharacterMovementComponent::CanUseShipBasedClientPosition(
	const FVector& RelativeClientLocation,
	UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName,
	uint8 ClientMovementMode,
	float& OutRelativeError) const
{
	OutRelativeError = TNumericLimits<float>::Max();

	if (ShipBasedClientAuthorityMaxError <= 0.0f
		|| !CharacterOwner
		|| !UpdatedComponent
		|| MovementMode != MOVE_Walking
		|| PackNetworkMovementMode() != ClientMovementMode
		|| !ClientMovementBase)
	{
		return false;
	}

	UPrimitiveComponent* ServerMovementBase = CharacterOwner->GetMovementBase();
	AShip* ClientShip = Cast<AShip>(ClientMovementBase->GetOwner());
	AShip* ServerShip = ServerMovementBase ? Cast<AShip>(ServerMovementBase->GetOwner()) : nullptr;
	if (ServerShip)
	{
		const_cast<USWCharacterMovementComponent*>(this)->LastStandingShip = ServerShip;
	}
	if (!ClientShip || ClientShip != ServerShip
		|| ClientBaseBoneName != CharacterOwner->GetBasedMovement().BoneName
		|| !MovementBaseUtility::UseRelativeLocation(ClientMovementBase)
		|| !MovementBaseUtility::UseRelativeLocation(ServerMovementBase))
	{
		return false;
	}

	FVector ServerRelativeLocation = FVector::ZeroVector;
	MovementBaseUtility::TransformLocationToLocal(
		ServerMovementBase,
		CharacterOwner->GetBasedMovement().BoneName,
		UpdatedComponent->GetComponentLocation(),
		ServerRelativeLocation);

	OutRelativeError = FVector::Distance(ServerRelativeLocation, RelativeClientLocation);
	return OutRelativeError <= ShipBasedClientAuthorityMaxError;
}
