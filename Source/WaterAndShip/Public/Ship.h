#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Engine/DataTable.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Ship.generated.h"

USTRUCT()
struct FNetInputShip : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	FNetInputShip() 
		: MovementInput(0.f)
		, SteeringInput(0.f)
		, GravityZ(-980.f)
		, LateralDrag(0.5f)
		, ForwardForceValue(500000.f)
		, TurnTorqueValue(20000000.f)
		, SpeedMultiplier(1.0f)
		, BuoyancyRadius(100.f)
		, BuoyancyForceMultiplier(1.2f)
		, WaterDamping(3.0f)
		, SpawnWorldTime(-1.f)
		, SpawnPhysicsStep(-1)
	{}

	void Reset()
	{
		MovementInput = 0.0f;
		SteeringInput = 0.0f;
		PontoonOffsets.Empty();
		GerstnerWaves.Empty();
		SpawnWorldTime = -1.0f;
		SpawnPhysicsStep = -1;
	}

	UPROPERTY()
	float MovementInput;

	UPROPERTY()
	float SteeringInput;

	// 로컬 마샬링 전용 비복제 물리/부력 필드들 (NetSerialize에서는 스킵)
	TArray<FVector> PontoonOffsets;
	TArray<FGerstnerWave> GerstnerWaves;
	float GravityZ;
	float LateralDrag;
	float ForwardForceValue;
	float TurnTorqueValue;
	float SpeedMultiplier;
	float BuoyancyRadius;
	float BuoyancyForceMultiplier;
	float WaterDamping;
	float WaterDamping2;
	float SpawnWorldTime;
	int32 SpawnPhysicsStep;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData, const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override
	{
		const FNetInputShip& MinInput = static_cast<const FNetInputShip&>(MinData);
		const FNetInputShip& MaxInput = static_cast<const FNetInputShip&>(MaxData);
		MovementInput = FMath::Lerp(MinInput.MovementInput, MaxInput.MovementInput, LerpAlpha);
		SteeringInput = FMath::Lerp(MinInput.SteeringInput, MaxInput.SteeringInput, LerpAlpha);
		
		// 비보간 데이터는 단순 이전/이후 값 대입
		PontoonOffsets = MaxInput.PontoonOffsets;
		GerstnerWaves = MaxInput.GerstnerWaves;
		GravityZ = MaxInput.GravityZ;
		LateralDrag = MaxInput.LateralDrag;
		ForwardForceValue = MaxInput.ForwardForceValue;
		TurnTorqueValue = MaxInput.TurnTorqueValue;
		SpeedMultiplier = MaxInput.SpeedMultiplier;
		BuoyancyRadius = MaxInput.BuoyancyRadius;
		BuoyancyForceMultiplier = MaxInput.BuoyancyForceMultiplier;
		WaterDamping = MaxInput.WaterDamping;
		WaterDamping2 = MaxInput.WaterDamping2;
		SpawnWorldTime = MaxInput.SpawnWorldTime;
		SpawnPhysicsStep = MaxInput.SpawnPhysicsStep;
	}

	virtual void MergeData(const FNetworkPhysicsPayload& FromData) override
	{
		const FNetInputShip& FromInput = static_cast<const FNetInputShip&>(FromData);
		MovementInput = FromInput.MovementInput;
		SteeringInput = FromInput.SteeringInput;
		
		PontoonOffsets = FromInput.PontoonOffsets;
		GerstnerWaves = FromInput.GerstnerWaves;
		GravityZ = FromInput.GravityZ;
		LateralDrag = FromInput.LateralDrag;
		ForwardForceValue = FromInput.ForwardForceValue;
		TurnTorqueValue = FromInput.TurnTorqueValue;
		SpeedMultiplier = FromInput.SpeedMultiplier;
		BuoyancyRadius = FromInput.BuoyancyRadius;
		BuoyancyForceMultiplier = FromInput.BuoyancyForceMultiplier;
		WaterDamping = FromInput.WaterDamping;
		WaterDamping2 = FromInput.WaterDamping2;
		SpawnWorldTime = FromInput.SpawnWorldTime;
		SpawnPhysicsStep = FromInput.SpawnPhysicsStep;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;
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
		return true;
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
	{}

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	FQuat Rotation;

	UPROPERTY()
	FVector LinearVelocity;

	UPROPERTY()
	FVector AngularVelocity;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData, const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override
	{
		const FNetStatePhysicsShip& MinState = static_cast<const FNetStatePhysicsShip&>(MinData);
		const FNetStatePhysicsShip& MaxState = static_cast<const FNetStatePhysicsShip&>(MaxData);

		Position = FMath::Lerp(MinState.Position, MaxState.Position, LerpAlpha);
		Rotation = FQuat::Slerp(MinState.Rotation, MaxState.Rotation, LerpAlpha);
		LinearVelocity = FMath::Lerp(MinState.LinearVelocity, MaxState.LinearVelocity, LerpAlpha);
		AngularVelocity = FMath::Lerp(MinState.AngularVelocity, MaxState.AngularVelocity, LerpAlpha);
	}

	virtual bool CompareData(const FNetworkPhysicsPayload& PredictedData) const override
	{
		const FNetStatePhysicsShip& PredState = static_cast<const FNetStatePhysicsShip&>(PredictedData);

		// 배의 롤백 트리거 위치/회전 차이 임계값
		if (FVector::DistSquared(Position, PredState.Position) > 25.0f) return false;
		if (Rotation.AngularDistance(PredState.Rotation) > 0.035f) return false; // 약 2도

		return true;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;
		Ar << Position;
		Ar << Rotation;
		Ar << LinearVelocity;
		Ar << AngularVelocity;
		return true;
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

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/* Boarding Interaction */
	void Board(APawn* PlayerPawn);

	/* Components */

	/** Root physics mesh for the ship */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* BuoyancyRoot;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float ForwardForce = 500000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float TurnTorque = 50000000.f;

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
	void ShipTurn(const FInputActionValue& Value);
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

	float CurrentMoveInput = 0.0f;
	float CurrentTurnInput = 0.0f;

	bool bStaticDataInitialized = false;
};
