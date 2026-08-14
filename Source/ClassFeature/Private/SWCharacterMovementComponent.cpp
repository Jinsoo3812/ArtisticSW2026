#include "SWCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Ship.h"
#include "SwimmingComponent.h"

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

void USWCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->CheckWaterTransitions();
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

	float RelativeError = 0.0f;
	if (CanUseShipBasedClientPosition(
		RelativeClientLocation,
		ClientMovementBase,
		ClientBaseBoneName,
		ClientMovementMode,
		RelativeError))
	{
		UE_CLOG(IsShipJitterDiagnosticsCommandEnabled(), LogTemp, Warning,
			TEXT("[SHIP-CMC-RELATIVE-ACCEPT] Character=%s Ship=%s Error=%.3fcm Limit=%.3fcm Time=%.3f"),
			*GetNameSafe(CharacterOwner), *GetNameSafe(ClientMovementBase->GetOwner()),
			RelativeError, ShipBasedClientAuthorityMaxError, ClientTimeStamp);
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
		if (IsShipJitterDiagnosticsCommandEnabled()
			&& GetWorld()
			&& GetWorld()->GetTimeSeconds() >= NextShipBasedSyncDiagnosticTime)
		{
			NextShipBasedSyncDiagnosticTime = GetWorld()->GetTimeSeconds() + 5.0;
			UE_LOG(LogTemp, Warning,
				TEXT("[SHIP-CMC-RELATIVE-SYNC] Character=%s Ship=%s Error=%.3fcm Limit=%.3fcm Time=%.3f"),
				*GetNameSafe(CharacterOwner), *GetNameSafe(ClientMovementBase->GetOwner()),
				RelativeError, ShipBasedClientAuthorityMaxError, ClientTimeStamp);
		}

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
