#include "SWCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Ship.h"
#include "SwimmingComponent.h"

namespace
{
	class FSavedMove_SWCharacter final : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;

		uint8 bSavedSwimDive : 1;
		uint8 bSavedSwimAscend : 1;
		ESwimDepthMode SavedSwimDepthMode = ESwimDepthMode::Surface;

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
			SavedSwimDepthMode = ESwimDepthMode::Surface;
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
				|| SavedSwimDepthMode != NewSWMove->SavedSwimDepthMode)
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
			if (const USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				const float VerticalInput = Movement->GetSwimmingVerticalInput();
				bSavedSwimDive = VerticalInput < -KINDA_SMALL_NUMBER;
				bSavedSwimAscend = VerticalInput > KINDA_SMALL_NUMBER;
				SavedSwimDepthMode = Movement->GetSwimmingDepthMode();
			}
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			if (USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				const float VerticalInput = bSavedSwimDive ? -1.0f : (bSavedSwimAscend ? 1.0f : 0.0f);
				Movement->RestoreSavedSwimmingState(VerticalInput, SavedSwimDepthMode);
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

USWCharacterMovementComponent::USWCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
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

ESwimDepthMode USWCharacterMovementComponent::GetSwimmingDepthMode() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp =
			CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetDepthMode();
		}
	}
	return ESwimDepthMode::Surface;
}

void USWCharacterMovementComponent::RestoreSavedSwimmingState(
	float InVerticalInput,
	ESwimDepthMode InDepthMode)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->RestorePredictedDepthMode(InDepthMode);
			SwimComp->SetVerticalSwimInput(InVerticalInput);
		}
	}
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
	const float VerticalInput = bDive ? -1.0f : (bAscend ? 1.0f : 0.0f);
	SetSwimmingVerticalInput(VerticalInput);
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
