#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Engine/DataTable.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "Ship.generated.h"

class USWBuoyancyComponent;
USTRUCT()
struct FNetInputShip : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	FNetInputShip() 
		: MovementInput(0.f)
		, SteeringInput(0.f)
		, ExternalAcceleration(FVector::ZeroVector)
		, bBuoyancyEnabled(true)
		, bHasAuthoritativeBuoyancyState(false)
		, bIsAnchorDropped(false)
		, AnchorOriginXY(FVector2D::ZeroVector)
		{}

	void Reset()
	{
		MovementInput = 0.0f;
		SteeringInput = 0.0f;
		ExternalAcceleration = FVector::ZeroVector;
		bBuoyancyEnabled = true;
		bHasAuthoritativeBuoyancyState = false;
		bIsAnchorDropped = false;
		AnchorOriginXY = FVector2D::ZeroVector;
	}

	UPROPERTY()
	float MovementInput;

	UPROPERTY()
	float SteeringInput;

	/** Server-authored world-space acceleration replayed by Network Physics. */
	UPROPERTY()
	FVector ExternalAcceleration;

	/** Authoritative per-frame buoyancy state replayed during rollback. */
	UPROPERTY()
	bool bBuoyancyEnabled;

	/** Prevents client-authored input from overwriting the server's buoyancy state. */
	UPROPERTY()
	bool bHasAuthoritativeBuoyancyState;

	UPROPERTY()
	bool bIsAnchorDropped;

	UPROPERTY()
	FVector2D AnchorOriginXY;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData, const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override
	{
		const FNetInputShip& MinInput = static_cast<const FNetInputShip&>(MinData);
		const FNetInputShip& MaxInput = static_cast<const FNetInputShip&>(MaxData);
		MovementInput = FMath::Lerp(MinInput.MovementInput, MaxInput.MovementInput, LerpAlpha);
		SteeringInput = FMath::Lerp(MinInput.SteeringInput, MaxInput.SteeringInput, LerpAlpha);
		ExternalAcceleration = FMath::Lerp(MinInput.ExternalAcceleration, MaxInput.ExternalAcceleration, LerpAlpha);
		bBuoyancyEnabled = LerpAlpha < 0.5f
			? MinInput.bBuoyancyEnabled
			: MaxInput.bBuoyancyEnabled;
		bHasAuthoritativeBuoyancyState = LerpAlpha < 0.5f
			? MinInput.bHasAuthoritativeBuoyancyState
			: MaxInput.bHasAuthoritativeBuoyancyState;
		bIsAnchorDropped = LerpAlpha < 0.5f
			? MinInput.bIsAnchorDropped
			: MaxInput.bIsAnchorDropped;
		AnchorOriginXY = MaxInput.AnchorOriginXY;
	}

	virtual void MergeData(const FNetworkPhysicsPayload& FromData) override
	{
		const FNetInputShip& FromInput = static_cast<const FNetInputShip&>(FromData);
		MovementInput = FromInput.MovementInput;
		SteeringInput = FromInput.SteeringInput;
		ExternalAcceleration = FromInput.ExternalAcceleration;
		bBuoyancyEnabled = FromInput.bBuoyancyEnabled;
		bHasAuthoritativeBuoyancyState = FromInput.bHasAuthoritativeBuoyancyState;
		bIsAnchorDropped = FromInput.bIsAnchorDropped;
		AnchorOriginXY = FromInput.AnchorOriginXY;
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

			uint8 bHasExternalAcceleration = ExternalAcceleration.IsNearlyZero(0.5f) ? 0 : 1;
			Ar.SerializeBits(&bHasExternalAcceleration, 1);
			if (bHasExternalAcceleration != 0)
			{
				int16 QuantizedX = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(ExternalAcceleration.X), -32767, 32767));
				int16 QuantizedY = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(ExternalAcceleration.Y), -32767, 32767));
				Ar << QuantizedX;
				Ar << QuantizedY;
			}

			uint8 SerializedAnchorDropped = bIsAnchorDropped ? 1 : 0;
			Ar.SerializeBits(&SerializedAnchorDropped, 1);
			if (SerializedAnchorDropped != 0)
			{
				float AnchorX = static_cast<float>(AnchorOriginXY.X);
				float AnchorY = static_cast<float>(AnchorOriginXY.Y);
				Ar << AnchorX;
				Ar << AnchorY;
			}
		}
		else
		{
			int8 QuantizedMove = 0;
			int8 QuantizedSteer = 0;
			Ar << QuantizedMove;
			Ar << QuantizedSteer;
			MovementInput = static_cast<float>(QuantizedMove) / 127.f;
			SteeringInput = static_cast<float>(QuantizedSteer) / 127.f;

			uint8 bHasExternalAcceleration = 0;
			Ar.SerializeBits(&bHasExternalAcceleration, 1);
			if (bHasExternalAcceleration != 0)
			{
				int16 QuantizedX = 0;
				int16 QuantizedY = 0;
				Ar << QuantizedX;
				Ar << QuantizedY;
				ExternalAcceleration = FVector(static_cast<float>(QuantizedX), static_cast<float>(QuantizedY), 0.0f);
			}
			else
			{
				ExternalAcceleration = FVector::ZeroVector;
			}

			uint8 SerializedAnchorDropped = 0;
			Ar.SerializeBits(&SerializedAnchorDropped, 1);
			bIsAnchorDropped = SerializedAnchorDropped != 0;
			if (bIsAnchorDropped)
			{
				float AnchorX = 0.0f;
				float AnchorY = 0.0f;
				Ar << AnchorX;
				Ar << AnchorY;
				AnchorOriginXY = FVector2D(AnchorX, AnchorY);
			}
			else
			{
				AnchorOriginXY = FVector2D::ZeroVector;
			}
		}

		uint8 SerializedBuoyancyEnabled = bBuoyancyEnabled ? 1 : 0;
		uint8 SerializedHasAuthoritativeBuoyancyState = bHasAuthoritativeBuoyancyState ? 1 : 0;
		Ar.SerializeBits(&SerializedBuoyancyEnabled, 1);
		Ar.SerializeBits(&SerializedHasAuthoritativeBuoyancyState, 1);
		if (Ar.IsLoading())
		{
			bBuoyancyEnabled = SerializedBuoyancyEnabled != 0;
			bHasAuthoritativeBuoyancyState = SerializedHasAuthoritativeBuoyancyState != 0;
		}

		bOutSuccess = !Ar.IsError();
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
class USceneComponent;
class UAbilitySystemComponent;
class UBaseAttributeSet;
class UShipAttributeSet;
class UGameplayAbility;
class ABombardment;
class ABombardmentPreview;
class ACannon;

USTRUCT(BlueprintType)
struct FShipStatRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 100.f;

	/** Legacy migration source. New runtime code does not consume this field directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (DeprecatedProperty, DeprecationMessage = "Use ForwardPropulsionMultiplier and TurnTorqueMultiplier"))
	float ShipSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float ForwardPropulsionMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float TurnTorqueMultiplier = 1.f;

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

	UFUNCTION(BlueprintPure, Category = "Ship|Stats")
	UShipAttributeSet* GetShipAttributeSet() const { return AttributeSet; }

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

	/** Returns the selected DT row, with legacy movement fields safely migrated in memory. */
	UFUNCTION(BlueprintPure, Category = "Ship|Stats")
	FShipStatSnapshot GetBaseStatSnapshot() const;

	/** Applies an already calculated authoritative snapshot to this ship. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Stats")
	void ApplyStatSnapshot(const FShipStatSnapshot& Snapshot, bool bRefillHealth = true);

	/** Applies the assigned player's active upgrade nodes over this ship's base DT row. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Stats")
	bool ApplyPlayerUpgrades(APlayerState* InPlayerState, bool bRefillHealth = true);

	/**
	 * Sets normalized server-authored control input for AI-controlled ships.
	 * The optional scales multiply the DT/ASC-backed physical force after input
	 * clamping, allowing temporary skills such as charge to exceed normal thrust.
	 */
	void SetAIControlInput(
		float MoveInput,
		float TurnInput,
		float PropulsionScale = 1.0f,
		float TurnScale = 1.0f);

	float GetCurrentAIPropulsionScale() const { return CurrentAIPropulsionScale; }
	float GetCurrentAITurnScale() const { return CurrentAITurnScale; }

	/** Identifies hostile ships without making WaterAndShip depend on Enemy. */
	virtual bool IsEnemyShipForEffects() const { return false; }

	/** Class policy used by interaction collision and the authoritative Board guard. */
	UFUNCTION(BlueprintPure, Category = "Ship|Control")
	virtual bool AllowsPlayerHelmControl() const { return true; }

	/** Class policy inherited by every cannon mounted on this ship. */
	UFUNCTION(BlueprintPure, Category = "Ship|Control")
	virtual bool AllowsPlayerCannonControl() const { return true; }

	/** Class policy used by reusable and legacy sea-boarding points. */
	UFUNCTION(BlueprintPure, Category = "Ship|Control")
	virtual bool AllowsPlayerBoarding() const { return true; }

	void SetExternalAccelerationSource(const FGuid& SourceId, const FVector& WorldAcceleration);
	void RemoveExternalAccelerationSource(const FGuid& SourceId);

	UFUNCTION(BlueprintPure, Category = "Ship|Effects")
	int32 GetExternalAccelerationSourceCount() const { return ExternalAccelerationSources.Num(); }

	UFUNCTION(BlueprintPure, Category = "Ship|Effects")
	FVector GetCurrentExternalAcceleration() const { return CurrentExternalAcceleration; }

	void AddPropulsionSuppression(const FGuid& SourceId);
	void RemovePropulsionSuppression(const FGuid& SourceId);
	bool IsPropulsionSuppressed() const { return PropulsionSuppressionSources.Num() > 0; }

	/** Bombardment GA bridge. The WaterAndShip module stays independent from ClassFeature. */
	bool ActivateBombardmentModeFromAbility(
		UGameplayAbility* Ability,
		TSubclassOf<ABombardment> BombardmentClass);
	void DeactivateBombardmentModeFromAbility(UGameplayAbility* Ability);
	bool IsBombardmentTargeting() const { return bBombardmentTargeting; }
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBombardmentTargetingChanged, bool);
	FOnBombardmentTargetingChanged OnBombardmentTargetingChanged;
	APawn* GetRidingPlayer() const { return RidingPlayer; }

	UFUNCTION(BlueprintPure, Category = "Ship|Helm")
	bool IsHelmOccupied() const { return RidingPlayer != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Ship|Helm")
	UInteractableComponent* GetHelmInteractable() const { return HelmInteractable; }

	UFUNCTION(BlueprintPure, Category = "Ship|Helm")
	USceneComponent* GetHelmSeatPoint() const { return HelmSeatPoint; }

	UFUNCTION(BlueprintPure, Category = "Ship|Helm")
	USceneComponent* GetHelmExitPoint() const { return HelmExitPoint; }

	UFUNCTION(BlueprintPure, Category = "Ship|Anchor")
	UStaticMeshComponent* GetAnchorMesh() const { return AnchorMesh; }

	UFUNCTION(BlueprintPure, Category = "Ship|Anchor")
	UInteractableComponent* GetAnchorInteractable() const { return AnchorInteractable; }

	UFUNCTION(BlueprintPure, Category = "Ship|Anchor")
	bool IsAnchorDropped() const { return bIsAnchorDropped; }

	UFUNCTION(BlueprintPure, Category = "Ship|Anchor")
	FVector2D GetAnchorOriginXY() const { return AnchorOriginXY; }

	UFUNCTION(BlueprintCallable, Category = "Ship|Anchor")
	void ToggleAnchor();

	UFUNCTION(Server, Reliable)
	void ServerToggleAnchor();

	UFUNCTION(BlueprintPure, Category = "Ship|Boarding")
	USceneComponent* GetBoardingArrivalPoint() const { return BoardingArrivalPoint; }

	/* Boarding Interaction */
	void Board(APawn* PlayerPawn);

	/** Teleports a character from any authored sea-boarding point to the shared arrival point. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Boarding")
	void BoardFromSea(AActor* Interactor);

	/** Safely returns the current helmsman to the authored exit point. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Helm")
	void ForceDisembark();

	/** Rebuilds the canonical runtime list from BP child actors and legacy attached cannon actors. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Cannons")
	void RefreshMountedCannons();

	UFUNCTION(BlueprintPure, Category = "Ship|Cannons")
	int32 GetMountedCannonCount() const { return MountedCannons.Num(); }

	const TArray<TObjectPtr<ACannon>>& GetMountedCannons() const { return MountedCannons; }

	/* Components */

	/**
	 * Root Chaos body for the ship. The serialized component name remains BuoyancyRoot
	 * so existing Blueprint mesh, mass, and Network Physics references stay intact.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (DisplayName = "Physics Root (BuoyancyRoot)"))
	UStaticMeshComponent* BuoyancyRoot;

	/** Collision-free visual mesh. Assigned independently from the Physics Root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipVisualMesh;

	/** Query-only receiver for the opposing cannon channel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipDamageMesh;

	/** Query-only walkable mesh. Assigned independently from the Physics Root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipDeckMesh;

	/** Shared pontoon/settings source; FShipPhysicsAsync remains the force executor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USWBuoyancyComponent> SWBuoyancyComponent;

	/** Camera boom for orbiting camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* CameraBoom;

	/** Follow camera attached to the boom */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* FollowCamera;

	/** Optional visible helm mesh. Its collision is independent from the interaction range. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Helm")
	TObjectPtr<UStaticMeshComponent> HelmMesh;

	/** Interaction range used only to enter ship-control mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Helm")
	TObjectPtr<UInteractableComponent> HelmInteractable;

	/** Exact relative transform used while the player controls the ship. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Helm")
	TObjectPtr<USceneComponent> HelmSeatPoint;

	/** Exact relative transform used when the player leaves ship-control mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Helm")
	TObjectPtr<USceneComponent> HelmExitPoint;

	/** Visible anchor mesh. Its transform and mesh can be authored in Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Anchor")
	TObjectPtr<UStaticMeshComponent> AnchorMesh;

	/** Interaction volume used to drop or raise the anchor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Anchor")
	TObjectPtr<UInteractableComponent> AnchorInteractable;

	/** Shared destination for every ShipBoardingPoint attached to this ship. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
	TObjectPtr<USceneComponent> BoardingArrivalPoint;

	/** Canonical runtime references for both BP child actors and legacy attached actors. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Ship|Cannons")
	TArray<TObjectPtr<ACannon>> MountedCannons;

	// Kept only so legacy test-ship Blueprints deserialize without hard errors.
	// BP_PlayerShip uses ShipBoardingPoint child actors and BoardingArrivalPoint.
	UPROPERTY(Transient, meta = (DeprecatedProperty, DeprecationMessage = "Use ShipBoardingPoint child actors"))
	TObjectPtr<UInteractableComponent> PortSeaBoardingInteractable;

	UPROPERTY(Transient, meta = (DeprecatedProperty, DeprecationMessage = "Use BoardingArrivalPoint"))
	TObjectPtr<USceneComponent> PortSeaBoardingDestination;

	UPROPERTY(Transient, meta = (DeprecatedProperty, DeprecationMessage = "Use ShipBoardingPoint child actors"))
	TObjectPtr<UInteractableComponent> StarboardSeaBoardingInteractable;

	UPROPERTY(Transient, meta = (DeprecatedProperty, DeprecationMessage = "Use BoardingArrivalPoint"))
	TObjectPtr<USceneComponent> StarboardSeaBoardingDestination;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float MaxExternalAcceleration = 5000.f;

	// ---- Anchor Parameters ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Anchor", meta = (ClampMin = "0.0", ToolTip = "Planar restoring stiffness holding the ship to its anchor point against external collisions"))
	float AnchorStiffness = 1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Anchor", meta = (ClampMin = "0.0", ToolTip = "Planar damping coefficient bringing horizontal velocity to a stop when anchored"))
	float AnchorDamping = 80000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Anchor", meta = (ClampMin = "0.0", Units = "cm", ToolTip = "Allowable horizontal slack distance before anchor spring tension applies"))
	float AnchorSlackRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Anchor", meta = (ClampMin = "0.0", ToolTip = "Maximum allowable horizontal force exerted by the anchor"))
	float MaxAnchorForce = 10000000.0f;

	// ---- Input Config ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputMappingContext* ShipInputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	int32 ShipInputPriority = 20;

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

	/** Axis1D action. Positive input (mouse wheel up) zooms in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipZoomAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input|Skills")
	UInputAction* ShipBombardmentToggleAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input|Skills")
	UInputAction* ShipBombardmentConfirmAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input|Skills")
	UInputAction* ShipBombardmentCancelAction;

	/** Ship look intentionally mirrors the on-foot camera convention. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera")
	bool bInvertShipLookYaw = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera")
	bool bInvertShipLookPitch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera", meta = (ClampMin = "0.0"))
	float ShipLookSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera|Zoom", meta = (ClampMin = "0.0", Units = "cm"))
	float ShipZoomStep = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera|Zoom", meta = (ClampMin = "0.0", Units = "cm"))
	float MinShipZoomArmLength = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Camera|Zoom", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxShipZoomArmLength = 1800.0f;

	/**
	 * Smooth only the presented camera rotation. ControlRotation still receives the
	 * full mouse delta immediately, so gameplay aim and network input are not delayed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Camera|Rotation Smoothing")
	bool bEnableCameraRotationSmoothing = true;

	/** Larger values follow ControlRotation faster. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Camera|Rotation Smoothing", meta = (EditCondition = "bEnableCameraRotationSmoothing", ClampMin = "0.0", UIMin = "0.0"))
	float CameraRotationSmoothingSpeed = 35.f;

	/** Maximum integration step used by SpringArm rotation-lag substepping. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Camera|Rotation Smoothing", meta = (EditCondition = "bEnableCameraRotationSmoothing", ClampMin = "0.001", UIMin = "0.001", Units = "s"))
	float CameraRotationSmoothingMaxTimeStep = 0.008333333f;

protected:
	// ---- Input Handlers ----
	void ShipMove(const FInputActionValue& Value);
	void StopShipMove(const FInputActionValue& Value);
	void ShipTurn(const FInputActionValue& Value);
	void StopShipTurn(const FInputActionValue& Value);
	void ShipLook(const FInputActionValue& Value);
	void ShipZoom(const FInputActionValue& Value);
	void ToggleFixedCamera();
	void OnDisembarkAction(const FInputActionValue& Value);
	void HandleBombardmentToggle();
	void HandleBombardmentConfirm();
	void HandleBombardmentCancel();

	UFUNCTION()
	void HandlePortSeaBoarding(AActor* Interactor);

	UFUNCTION()
	void HandleStarboardSeaBoarding(AActor* Interactor);

	UFUNCTION()
	void HandleAnchorInteracted(AActor* Interactor);

	void UpdateAnchorInteractionUI();

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

	UFUNCTION(Server, Reliable)
	void ServerToggleBombardmentAbility();

	UFUNCTION(Server, Reliable)
	void ServerConfirmBombardment(FVector ClientTargetLocation);

	UFUNCTION(Server, Reliable)
	void ServerCancelBombardmentAbility();

	void Disembark();
	void UpdateHelmInteractionAvailability();

	// ---- Camera State ----
	bool bUsingFixedCamera = false;
	FTransform SavedBoomRelativeTransform;
	FRotator SavedControlRotation;
	float SavedTargetArmLength = 800.0f;
	FVector SavedFollowCameraRelativeLocation = FVector::ZeroVector;
	FRotator SavedFollowCameraRelativeRotation = FRotator::ZeroRotator;
	bool bHasRememberedFollowCameraState = false;
	float RememberedFollowTargetArmLength = 800.0f;
	FRotator RememberedFollowControlRotation = FRotator::ZeroRotator;

	void RememberFollowCameraState(APlayerController* PlayerController);
	void RestoreRememberedFollowCameraState(APlayerController* PlayerController);

	// ---- Passenger Reference ----
	UPROPERTY(ReplicatedUsing = OnRep_RidingPlayer)
	APawn* RidingPlayer = nullptr;

	UFUNCTION()
	void OnRep_RidingPlayer(APawn* OldRidingPlayer);

	UPROPERTY(ReplicatedUsing = OnRep_IsAnchorDropped)
	bool bIsAnchorDropped = false;

	UPROPERTY(Replicated)
	FVector2D AnchorOriginXY = FVector2D::ZeroVector;

	UFUNCTION()
	void OnRep_IsAnchorDropped();

	UPROPERTY(ReplicatedUsing = OnRep_BombardmentTargeting)
	bool bBombardmentTargeting = false;

	UPROPERTY(ReplicatedUsing = OnRep_BombardmentTargeting)
	TSubclassOf<ABombardment> ActiveBombardmentClass;

	UFUNCTION()
	void OnRep_BombardmentTargeting();

	UPROPERTY()
	APlayerController* CachedPlayerController = nullptr;

	/** Prevents repeated boarding by the same player from refilling upgraded health. */
	UPROPERTY(Transient)
	TObjectPtr<APlayerState> AppliedUpgradePlayerState;

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
	void ToggleBombardmentAbilityAuthoritative();
	void CancelBombardmentAbilityAuthoritative();
	void SetBombardmentTargetingAuthoritative(bool bEnabled);
	UAbilitySystemComponent* GetRidingPlayerAbilitySystem() const;
	TSubclassOf<AActor> ResolveNormalCannonballClass() const;
	bool ValidateAndResolveBombardmentTarget(const FVector& RequestedLocation, FVector& OutLocation) const;
	bool ResolveBombardmentTargetFromCursor(FVector& OutLocation) const;
	bool ResolveStableSurfaceAtXY(const FVector2D& XY, FVector& OutLocation) const;
	bool FindLandscapeHitAlongRay(const FVector& RayStart, const FVector& RayEnd, FHitResult& OutHit) const;
	void RefreshLocalBombardmentTargeting();
	void BeginLocalBombardmentTargeting();
	void EndLocalBombardmentTargeting();
	void UpdateLocalBombardmentPreview();
	void SpawnBombardmentAuthoritative(const FVector& TargetLocation);

	TWeakObjectPtr<UGameplayAbility> ActiveBombardmentAbility;

	UPROPERTY(Transient)
	TObjectPtr<ABombardmentPreview> BombardmentPreviewActor;

	FVector LocalBombardmentTarget = FVector::ZeroVector;
	bool bLocalBombardmentTargetValid = false;
	bool bLocalBombardmentInputModeApplied = false;
	bool bSavedShowMouseCursor = false;

	friend class FShipPhysicsAsync;
	FShipPhysicsAsync* ShipPhysicsAsync = nullptr;
	bool bBuoyancyQueryDiagnostics = false;
	double NextBuoyancyQueryDiagnosticTime = 0.0;

	float CurrentMoveInput = 0.0f;
	float CurrentTurnInput = 0.0f;

	/** Server-authored transient force scales must match on simulated proxies during Network Physics resimulation. */
	UPROPERTY(Replicated)
	float CurrentAIPropulsionScale = 1.0f;

	UPROPERTY(Replicated)
	float CurrentAITurnScale = 1.0f;
	FVector CurrentExternalAcceleration = FVector::ZeroVector;
	TMap<FGuid, FVector> ExternalAccelerationSources;
	TSet<FGuid> PropulsionSuppressionSources;

	bool bStaticDataInitialized = false;
};
