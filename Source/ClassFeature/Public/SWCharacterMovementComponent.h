#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SWCharacterMovementComponent.generated.h"

/**
 * Custom character movement component that handles custom movement modes,
 * specifically custom swimming movement to support smooth client prediction.
 */
UCLASS()
class CLASSFEATURE_API USWCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual bool ServerExceedsAllowablePositionError(
		float ClientTimeStamp,
		float DeltaTime,
		const FVector& Accel,
		const FVector& ClientWorldLocation,
		const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName,
		uint8 ClientMovementMode) override;
	virtual bool ServerShouldUseAuthoritativePosition(
		float ClientTimeStamp,
		float DeltaTime,
		const FVector& Accel,
		const FVector& ClientWorldLocation,
		const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName,
		uint8 ClientMovementMode) override;

	/**
	 * Maximum base-local disagreement that the server may absorb while both
	 * sides agree that the character is walking on the same predicted ship.
	 * This is deliberately separate from the global CMC position tolerance.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Movement: Networking|Ship Base", meta = (ClampMin = "0.0", Units = "cm"))
	float ShipBasedClientAuthorityMaxError = 15.0f;

private:
	double NextShipBasedSyncDiagnosticTime = 0.0;

	bool CanUseShipBasedClientPosition(
		const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName,
		uint8 ClientMovementMode,
		float& OutRelativeError) const;
};
