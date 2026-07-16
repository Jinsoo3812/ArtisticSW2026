#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Engine/DataTable.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Ship.generated.h"

class USWBuoyancyComponent;

USTRUCT()
struct FNetInputShip : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	FNetInputShip() 
		: MovementInput(0.f)
		, SteeringInput(0.f)
	{}

	void Reset()
	{
		MovementInput = 0.0f;
		SteeringInput = 0.0f;
	}

	UPROPERTY()
	float MovementInput;

	UPROPERTY()
	float SteeringInput;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData, const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override
	{
		const FNetInputShip& MinInput = static_cast<const FNetInputShip&>(MinData);
		const FNetInputShip& MaxInput = static_cast<const FNetInputShip&>(MaxData);
		MovementInput = FMath::Lerp(MinInput.MovementInput, MaxInput.MovementInput, LerpAlpha);
		SteeringInput = FMath::Lerp(MinInput.SteeringInput, MaxInput.SteeringInput, LerpAlpha);
	}

	virtual void MergeData(const FNetworkPhysicsPayload& FromData) override
	{
		const FNetInputShip& FromInput = static_cast<const FNetInputShip&>(FromData);
		MovementInput = FromInput.MovementInput;
		SteeringInput = FromInput.SteeringInput;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		// FInstancedStruct calls this native serializer directly, so inherited
		// FNetworkPhysicsPayload fields are not serialized automatically.
		// ServerFrame is the identity used to map authoritative data to the
		// matching client rewind-history frame.
		uint32 PackedServerFrame = 0;
		if (Ar.IsSaving())
		{
			if (!ensureMsgf(ServerFrame >= -1, TEXT("FNetInputShip cannot serialize invalid ServerFrame %d"), ServerFrame))
			{
				bOutSuccess = false;
				return false;
			}

			PackedServerFrame = static_cast<uint32>(static_cast<int64>(ServerFrame) + 1);
		}

		Ar.SerializeIntPacked(PackedServerFrame);

		if (Ar.IsLoading())
		{
			const int64 DecodedServerFrame = static_cast<int64>(PackedServerFrame) - 1;
			if (!ensureMsgf(DecodedServerFrame <= MAX_int32, TEXT("FNetInputShip received invalid packed ServerFrame %u"), PackedServerFrame))
			{
				bOutSuccess = false;
				return false;
			}

			ServerFrame = static_cast<int32>(DecodedServerFrame);
			LocalFrame = ServerFrame;
		}

		if (Ar.IsSaving())
		{
			int8 QuantizedMove = FMath::Clamp(FMath::RoundToInt(MovementInput * 127.f), -128, 127);
			int8 QuantizedSteer = FMath::Clamp(FMath::RoundToInt(SteeringInput * 127.f), -128, 127);
			Ar << QuantizedMove;
			Ar << QuantizedSteer;
		}
		else
		{
			int8 QuantizedMove = 0;
			int8 QuantizedSteer = 0;
			Ar << QuantizedMove;
			Ar << QuantizedSteer;
			MovementInput = static_cast<float>(QuantizedMove) / 127.f;
			SteeringInput = static_cast<float>(QuantizedSteer) / 127.f;
		}

		bOutSuccess = !Ar.IsError();
		/* Network Physics serializer diagnostic log disabled after validation.
		if (bOutSuccess && ServerFrame > 0 && (ServerFrame <= 5 || ServerFrame % 60 == 0))
		{
			UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-FRAME-INPUT] Direction=%s ServerFrame=%d LocalFrame=%d Move=%.3f Steer=%.3f"),
				Ar.IsLoading() ? TEXT("Load") : TEXT("Save"),
				ServerFrame,
				LocalFrame,
				MovementInput,
				SteeringInput);
		}
		*/

		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits<FNetInputShip> : public TStructOpsTypeTraitsBase2<FNetInputShip>
{
	enum
	{
		WithNetSerializer = true,
	};
};

USTRUCT()
struct FNetStatePhysicsShip : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	FNetStatePhysicsShip()
		: Position(FVector::ZeroVector)
		, Rotation(FQuat::Identity)
		, LinearVelocity(FVector::ZeroVector)
		, AngularVelocity(FVector::ZeroVector)
		, LocationThresholdSq(25.0f) // 기본 5cm의 제곱
		, RotationThresholdRad(0.087f) // 기본 5도의 라디안
	{}

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	FQuat Rotation;

	UPROPERTY()
	FVector LinearVelocity;

	UPROPERTY()
	FVector AngularVelocity;

	UPROPERTY()
	float LocationThresholdSq;

	UPROPERTY()
	float RotationThresholdRad;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData, const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override
	{
		const FNetStatePhysicsShip& MinState = static_cast<const FNetStatePhysicsShip&>(MinData);
		const FNetStatePhysicsShip& MaxState = static_cast<const FNetStatePhysicsShip&>(MaxData);

		Position = FMath::Lerp(MinState.Position, MaxState.Position, LerpAlpha);
		Rotation = FQuat::Slerp(MinState.Rotation, MaxState.Rotation, LerpAlpha);
		LinearVelocity = FMath::Lerp(MinState.LinearVelocity, MaxState.LinearVelocity, LerpAlpha);
		AngularVelocity = FMath::Lerp(MinState.AngularVelocity, MaxState.AngularVelocity, LerpAlpha);
		LocationThresholdSq = MaxState.LocationThresholdSq;
		RotationThresholdRad = MaxState.RotationThresholdRad;
	}

	virtual bool CompareData(const FNetworkPhysicsPayload& PredictedData) const override
	{
		const FNetStatePhysicsShip& PredState = static_cast<const FNetStatePhysicsShip&>(PredictedData);

		// 배의 롤백 트리거 위치/회전 차이 임계값 (에디터 연동 변수 사용)
		float TargetLocThreshold = (LocationThresholdSq > 0.0f) ? LocationThresholdSq : 25.0f;
		float TargetRotThreshold = (RotationThresholdRad > 0.0f) ? RotationThresholdRad : 0.087f;

		float DistSq = FVector::DistSquared(Position, PredState.Position);
		float RotDist = Rotation.AngularDistance(PredState.Rotation);
		const bool bPositionMatch = DistSq <= TargetLocThreshold;
		const bool bRotationMatch = RotDist <= TargetRotThreshold;
		bool bMatch = bPositionMatch && bRotationMatch;
		/* Network Physics comparison diagnostic logs disabled after validation.
		float LinearVelocityDistSq = FVector::DistSquared(LinearVelocity, PredState.LinearVelocity);
		float AngularVelocityDist = FVector::Distance(AngularVelocity, PredState.AngularVelocity);
		const bool bPeriodicFrameLog = ServerFrame > 0 && (ServerFrame <= 5 || ServerFrame % 60 == 0);

		if (bPeriodicFrameLog || !bMatch)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-COMPARE] Result=%s AuthServerFrame=%d AuthLocalFrame=%d PredServerFrame=%d PredLocalFrame=%d Dist=%.2fcm Rot=%.3fdeg LinVel=%.2fcm/s AngVel=%.3fdeg/s"),
				bMatch ? TEXT("MATCH") : TEXT("MISMATCH"),
				ServerFrame,
				LocalFrame,
				PredState.ServerFrame,
				PredState.LocalFrame,
				FMath::Sqrt(DistSq),
				FMath::RadiansToDegrees(RotDist),
				FMath::Sqrt(LinearVelocityDistSq),
				FMath::RadiansToDegrees(AngularVelocityDist));
		}

		// 롤백이 격발되거나(오차 초과), 평상시용 진단 출력
		if (!bMatch)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RESIM-COMPARE-TRIGGER] AuthServerFrame=%d AuthLocalFrame=%d PredServerFrame=%d PredLocalFrame=%d ServerPos=%s ClientHistPos=%s Dist=%.2fcm Threshold=%.2fcm Rot=%.3fdeg RotThreshold=%.3fdeg LinVel=%.2fcm/s AngVel=%.3fdeg/s PosFail=%d RotFail=%d TriggerRollback=TRUE"),
				ServerFrame,
				LocalFrame,
				PredState.ServerFrame,
				PredState.LocalFrame,
				*Position.ToString(),
				*PredState.Position.ToString(),
				FMath::Sqrt(DistSq),
				FMath::Sqrt(TargetLocThreshold),
				FMath::RadiansToDegrees(RotDist),
				FMath::RadiansToDegrees(TargetRotThreshold),
				FMath::Sqrt(LinearVelocityDistSq),
				FMath::RadiansToDegrees(AngularVelocityDist),
				!bPositionMatch,
				!bRotationMatch);
		}
		*/

		return bMatch;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		// See FNetInputShip::NetSerialize. The authoritative state is only useful
		// when the receiver can associate it with the same predicted history frame.
		uint32 PackedServerFrame = 0;
		if (Ar.IsSaving())
		{
			if (!ensureMsgf(ServerFrame >= -1, TEXT("FNetStatePhysicsShip cannot serialize invalid ServerFrame %d"), ServerFrame))
			{
				bOutSuccess = false;
				return false;
			}

			PackedServerFrame = static_cast<uint32>(static_cast<int64>(ServerFrame) + 1);
		}

		Ar.SerializeIntPacked(PackedServerFrame);

		if (Ar.IsLoading())
		{
			const int64 DecodedServerFrame = static_cast<int64>(PackedServerFrame) - 1;
			if (!ensureMsgf(DecodedServerFrame <= MAX_int32, TEXT("FNetStatePhysicsShip received invalid packed ServerFrame %u"), PackedServerFrame))
			{
				bOutSuccess = false;
				return false;
			}

			ServerFrame = static_cast<int32>(DecodedServerFrame);
			LocalFrame = ServerFrame;
		}

		Ar << Position;
		Ar << Rotation;
		Ar << LinearVelocity;
		Ar << AngularVelocity;
		Ar << LocationThresholdSq;
		Ar << RotationThresholdRad;

		bOutSuccess = !Ar.IsError();
		/* Network Physics serializer diagnostic log disabled after validation.
		if (bOutSuccess && ServerFrame > 0 && (ServerFrame <= 5 || ServerFrame % 60 == 0))
		{
			UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-FRAME-STATE] Direction=%s ServerFrame=%d LocalFrame=%d Z=%.3f"),
				Ar.IsLoading() ? TEXT("Load") : TEXT("Save"),
				ServerFrame,
				LocalFrame,
				Position.Z);
		}
		*/

		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits<FNetStatePhysicsShip> : public TStructOpsTypeTraitsBase2<FNetStatePhysicsShip>
{
	enum
	{
		WithNetSerializer = true,
	};
};

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInteractableComponent;
class UInputMappingContext;
class UInputAction;
class APlayerController;
class UPrimitiveComponent;
class UAbilitySystemComponent;
class UBaseAttributeSet;
class UShipAttributeSet;

USTRUCT(BlueprintType)
struct FShipStatRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float ShipSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonDamage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonFireCooldown = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonballSpeed = 3000.f;
};

USTRUCT(BlueprintType)
struct FShipReplicatedState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;
};

UCLASS()
class WATERANDSHIP_API AShip : public APawn, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AShip();

	// IAbilitySystemInterface 구현
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Mirrors the legacy BuoyancyRoot asset into the split render/query meshes. */
	void SynchronizeSplitShipMeshes();

	/** Applies role-specific collision without re-enabling overlap on the physics root. */
	void ConfigureSplitShipCollision();

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Sets normalized server-authored control input for AI-controlled ships. */
	void SetAIControlInput(float MoveInput, float TurnInput);

	/* Boarding Interaction */
	void Board(APawn* PlayerPawn);

	/* Components */

	/**
	 * Root Chaos body for the ship. The serialized component name remains BuoyancyRoot
	 * so existing Blueprint mesh, mass, and Network Physics references stay intact.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (DisplayName = "Physics Root (BuoyancyRoot)"))
	UStaticMeshComponent* BuoyancyRoot;

	/** Collision-free visual copy of the Physics Root mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipVisualMesh;

	/** Query-only receiver for the opposing cannon channel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipDamageMesh;

	/** Query-only walkable copy; currently mirrors the root mesh until a deck-only asset is supplied. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipDeckMesh;

	/** Temporary migration switch. Disable after dedicated visual/damage/deck meshes are assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Mesh Split")
	bool bMirrorPhysicsRootMeshToSplitMeshes = true;

	/** Shared pontoon/settings source; FShipPhysicsAsync remains the force executor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USWBuoyancyComponent> SWBuoyancyComponent;

	/** Camera boom for orbiting camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* CameraBoom;

	/** Follow camera attached to the boom */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* FollowCamera;

	/** Interactable component to allow player interactions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UInteractableComponent* InteractableComponent;

	/** Port (left) side boarding interactable component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UInteractableComponent* PortSeaBoardingInteractable;

	/** Location point where the player will be teleported when boarding from the Port side */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* PortSeaBoardingDestination;

	/** Starboard (right) side boarding interactable component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UInteractableComponent* StarboardSeaBoardingInteractable;

	/** Location point where the player will be teleported when boarding from the Starboard side */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* StarboardSeaBoardingDestination;

	/** World location of the fixed observation camera (set XYZ in editor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Fixed Camera")
	FVector FixedCameraLocation = FVector(0.0f, 0.0f, 1000.0f);

	/** World rotation of the fixed observation camera (set in editor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Fixed Camera")
	FRotator FixedCameraRotation = FRotator(-45.0f, 0.0f, 0.0f);

	// ---- Movement Parameters ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Physics | Replication")
	float ResimLocationThreshold = 5.0f; // 오차 허용 거리 임계값 (cm 단위)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Physics | Replication")
	float ResimRotationThreshold = 5.0f; // 오차 허용 회전 임계값 (도 단위, Degree)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float ForwardForce = 2000000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float TurnTorque = 6000000000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float LateralDragCoefficient = 200.f;

	// ---- Input Config ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputMappingContext* ShipInputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	int32 ShipInputPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipMoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipTurnAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipToggleCameraAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipDisembarkAction;

protected:
	// ---- Input Handlers ----
	void ShipMove(const FInputActionValue& Value);
	void StopShipMove(const FInputActionValue& Value);
	void ShipTurn(const FInputActionValue& Value);
	void StopShipTurn(const FInputActionValue& Value);
	void ShipLook(const FInputActionValue& Value);
	void ToggleFixedCamera();
	void OnDisembarkAction(const FInputActionValue& Value);

	UFUNCTION()
	void HandlePortSeaBoarding(AActor* Interactor);

	UFUNCTION()
	void HandleStarboardSeaBoarding(AActor* Interactor);

	// Physics forces apply functions
	void ApplyForwardForce(float MoveValue);
	void ApplyTurnTorque(float TurnValue);

	// Server RPCs for movement (Unreliable because of high frequency axis updates)
	UFUNCTION(Server, Unreliable)
	void ServerMove(float MoveValue);

	UFUNCTION(Server, Unreliable)
	void ServerTurn(float TurnValue);

	// Axis release must not depend on an unreliable held-input packet arriving.
	UFUNCTION(Server, Reliable)
	void ServerStopMove();

	UFUNCTION(Server, Reliable)
	void ServerStopTurn();

	UFUNCTION(Server, Reliable)
	void ServerDisembark();

	void Disembark();

	// ---- Camera State ----
	bool bUsingFixedCamera = false;
	FTransform SavedBoomRelativeTransform;
	FRotator SavedControlRotation;
	float SavedTargetArmLength = 800.0f;
	FVector SavedFollowCameraRelativeLocation = FVector::ZeroVector;
	FRotator SavedFollowCameraRelativeRotation = FRotator::ZeroRotator;

	// ---- Passenger Reference ----
	UPROPERTY(ReplicatedUsing = OnRep_RidingPlayer)
	APawn* RidingPlayer = nullptr;

	UFUNCTION()
	void OnRep_RidingPlayer(APawn* OldRidingPlayer);

	UPROPERTY()
	APlayerController* CachedPlayerController = nullptr;

	// ---- Custom Replication State & Interp Configuration ----
	UPROPERTY(Replicated)
	FShipReplicatedState ReplicatedState;

	// Authoritative mapping from Network Physics ServerFrame 0 to server world time.
	// Late-joining clients combine this origin with the replicated solver step.
	UPROPERTY(Replicated)
	double ServerPhysicsTimeOrigin = -1.0;

	UPROPERTY(Replicated)
	float ServerPhysicsStepSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float LocationInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float RotationInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float TeleportThreshold = 500.0f;

public:
	virtual void OnRep_Controller() override;

	/** Returns CameraBoom subobject */
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Resets camera to follow mode (called when disembarking) */
	void ResetToFollowCamera();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stats")
	TObjectPtr<UDataTable> ShipStatTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stats")
	FName ShipStatRowName;

protected:
	void InitializeDefaultAttributes();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShipAttributeSet> AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNetworkPhysicsComponent> NetworkPhysicsComponent;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FShipPhysicsAsync;
	FShipPhysicsAsync* ShipPhysicsAsync = nullptr;
	bool bBuoyancyQueryDiagnostics = false;
	double NextBuoyancyQueryDiagnosticTime = 0.0;

	float CurrentMoveInput = 0.0f;
	float CurrentTurnInput = 0.0f;

	bool bStaticDataInitialized = false;
};
