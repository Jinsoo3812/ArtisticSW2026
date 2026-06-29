// Fill out your copyright notice in the Description page of Project Settings.


#include "Ship.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

// Sets default values
AShip::AShip()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Buoyancy Root
	BuoyancyRoot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuoyancyRoot"));
	RootComponent = BuoyancyRoot;
	BuoyancyRoot->SetSimulatePhysics(true);

	// Camera Boom
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(BuoyancyRoot);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;

	// Follow Camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Interactable Component
	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(BuoyancyRoot);
	// Set default collision preset for interactables
	InteractableComponent->SetCollisionProfileName(TEXT("Interactable"));

	bReplicates = true;
	SetReplicateMovement(false);
}

// Called when the game starts or when spawned
void AShip::BeginPlay()
{
	Super::BeginPlay();

	if (BuoyancyRoot)
	{
		if (HasAuthority())
		{
			BuoyancyRoot->SetSimulatePhysics(true);
		}
		else
		{
			BuoyancyRoot->SetSimulatePhysics(false);
		}
	}
}

// Called every frame
void AShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority())
	{
		// ---- Lateral Hydrodynamic Drag ----
		if (BuoyancyRoot && LateralDragCoefficient > 0.0f)
		{
			FVector Velocity = BuoyancyRoot->GetPhysicsLinearVelocity();

			// Ship's right vector projected onto XY plane
			FVector Right = GetActorRightVector();
			Right.Z = 0.0f;
			Right.Normalize();

			// Lateral speed = velocity component along the right axis
			float LateralSpeed = FVector::DotProduct(Velocity, Right);

			// Apply opposing force to resist sideways movement
			FVector LateralDragForce = -Right * LateralSpeed * LateralDragCoefficient;
			BuoyancyRoot->AddForce(LateralDragForce);
		}

		// Update replicated state for client-side interpolation
		ReplicatedState.Location = GetActorLocation();
		ReplicatedState.Rotation = GetActorRotation();
	}
	else
	{
		// Client-side interpolation towards server state
		FVector CurrentLocation = GetActorLocation();
		FQuat CurrentRotation = GetActorQuat();

		FVector TargetLocation = ReplicatedState.Location;
		FQuat TargetRotation = ReplicatedState.Rotation.Quaternion();

		float DistSq = FVector::DistSquared(CurrentLocation, TargetLocation);
		if (DistSq > FMath::Square(TeleportThreshold))
		{
			SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			FVector InterpedLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, LocationInterpSpeed);
			FQuat InterpedRotation = FMath::QInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationInterpSpeed);

			SetActorLocationAndRotation(InterpedLocation, InterpedRotation, false, nullptr, ETeleportType::None);
		}
	}
}

// Called to bind functionality to input
void AShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	CachedPlayerController = Cast<APlayerController>(GetController());

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Ship movement (W/S)
		if (ShipMoveAction)
		{
			EnhancedInput->BindAction(ShipMoveAction, ETriggerEvent::Triggered, this, &AShip::ShipMove);
		}

		// Ship turning (A/D)
		if (ShipTurnAction)
		{
			EnhancedInput->BindAction(ShipTurnAction, ETriggerEvent::Triggered, this, &AShip::ShipTurn);
		}

		// Ship camera look (Mouse)
		if (ShipLookAction)
		{
			EnhancedInput->BindAction(ShipLookAction, ETriggerEvent::Triggered, this, &AShip::ShipLook);
		}

		// Toggle fixed camera (C key)
		if (ShipToggleCameraAction)
		{
			EnhancedInput->BindAction(ShipToggleCameraAction, ETriggerEvent::Started, this, &AShip::ToggleFixedCamera);
		}

		// Disembark (F key)
		if (ShipDisembarkAction)
		{
			EnhancedInput->BindAction(ShipDisembarkAction, ETriggerEvent::Started, this, &AShip::OnDisembarkAction);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AShip::SetupPlayerInputComponent - Failed to find Enhanced Input Component."));
	}
}

void AShip::UnPossessed()
{
	Super::UnPossessed();
}

void AShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShip, RidingPlayer);
	DOREPLIFETIME(AShip, ReplicatedState);
}

void AShip::Board(APawn* PlayerPawn)
{
	if (!HasAuthority()) return;
	if (!PlayerPawn || RidingPlayer) return;

	APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController());
	if (!PC) return;

	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board initiated by player pawn %s. Ship location: %s, Player location: %s"), *PlayerPawn->GetName(), *GetActorLocation().ToString(), *PlayerPawn->GetActorLocation().ToString());

	RidingPlayer = PlayerPawn;

	// Disable player collision
	RidingPlayer->SetActorEnableCollision(false);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->DisableMovement();
		Char->GetCharacterMovement()->StopMovementImmediately();
	}

	// Disable movement replication while on the ship to prevent jittering
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player bReplicateMovement before disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));
	RidingPlayer->SetReplicateMovement(false);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player bReplicateMovement after disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Attach to buoyancy root directly without welding physics bodies to avoid physics conflicts
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
	RidingPlayer->AttachToComponent(BuoyancyRoot, AttachmentRules);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player attached to BuoyancyRoot. Relative location: %s, relative rotation: %s"), 
		*RidingPlayer->GetRootComponent()->GetRelativeLocation().ToString(), 
		*RidingPlayer->GetRootComponent()->GetRelativeRotation().ToString());

	// Possess ship pawn
	PC->Possess(this);
}

void AShip::OnDisembarkAction(const FInputActionValue& Value)
{
	ServerDisembark();
}

void AShip::ServerDisembark_Implementation()
{
	Disembark();
}

void AShip::Disembark()
{
	if (!HasAuthority()) return;
	if (!RidingPlayer) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark initiated. Player pawn: %s"), *RidingPlayer->GetName());

	// Restore camera mode
	ResetToFollowCamera();

	// Detach player preserving their current world position on the ship
	RidingPlayer->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark - Detached player. World location: %s"), *RidingPlayer->GetActorLocation().ToString());

	RidingPlayer->SetActorEnableCollision(true);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	// Restore movement replication on disembark
	RidingPlayer->SetReplicateMovement(true);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark - Player bReplicateMovement after enable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Return possession to player character
	PC->Possess(RidingPlayer);

	RidingPlayer = nullptr;
}

void AShip::ShipMove(const FInputActionValue& Value)
{
	const float MoveValue = Value.Get<float>();

	if (FMath::Abs(MoveValue) > KINDA_SMALL_NUMBER)
	{
		if (HasAuthority())
		{
			ApplyForwardForce(MoveValue);
		}
		else
		{
			ServerMove(MoveValue);
		}
	}
}

void AShip::ServerMove_Implementation(float MoveValue)
{
	ApplyForwardForce(MoveValue);
}

void AShip::ApplyForwardForce(float MoveValue)
{
	if (BuoyancyRoot && FMath::Abs(MoveValue) > KINDA_SMALL_NUMBER)
	{
		// Project forward vector onto XY plane so the ship always moves horizontally
		FVector Forward = GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward.Normalize();

		BuoyancyRoot->AddForce(Forward * ForwardForce * MoveValue);
	}
}

void AShip::ShipTurn(const FInputActionValue& Value)
{
	const float TurnValue = Value.Get<float>();

	if (FMath::Abs(TurnValue) > KINDA_SMALL_NUMBER)
	{
		if (HasAuthority())
		{
			ApplyTurnTorque(TurnValue);
		}
		else
		{
			ServerTurn(TurnValue);
		}
	}
}

void AShip::ServerTurn_Implementation(float TurnValue)
{
	ApplyTurnTorque(TurnValue);
}

void AShip::ApplyTurnTorque(float TurnValue)
{
	if (BuoyancyRoot && FMath::Abs(TurnValue) > KINDA_SMALL_NUMBER)
	{
		// Apply torque around the Z-axis (yaw) for horizontal turning
		BuoyancyRoot->AddTorqueInDegrees(FVector(0.0f, 0.0f, TurnTorque * TurnValue));
	}
}

void AShip::ShipLook(const FInputActionValue& Value)
{
	const FVector2D LookValue = Value.Get<FVector2D>();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Rotate the controller (which drives the SpringArm via bUsePawnControlRotation)
		PC->AddYawInput(LookValue.X);
		PC->AddPitchInput(LookValue.Y);
	}
}

void AShip::ToggleFixedCamera()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	bUsingFixedCamera = !bUsingFixedCamera;

	if (bUsingFixedCamera)
	{
		// Save current camera state before detaching
		SavedBoomRelativeTransform = CameraBoom->GetRelativeTransform();
		SavedTargetArmLength = CameraBoom->TargetArmLength;
		SavedControlRotation = PC->GetControlRotation();
		if (FollowCamera)
		{
			SavedFollowCameraRelativeLocation = FollowCamera->GetRelativeLocation();
			SavedFollowCameraRelativeRotation = FollowCamera->GetRelativeRotation();
		}

		// Detach camera boom so it stops following the ship
		CameraBoom->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		CameraBoom->bUsePawnControlRotation = false;

		// Move camera to fixed world position
		CameraBoom->SetWorldLocationAndRotation(FixedCameraLocation, FixedCameraRotation);
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(FVector::ZeroVector);
			FollowCamera->SetRelativeRotation(FRotator::ZeroRotator);
		}

		UE_LOG(LogTemp, Log, TEXT("Switched to fixed camera at %s"), *FixedCameraLocation.ToString());
	}
	else
	{
		// Re-attach camera boom to ship and restore saved state
		CameraBoom->AttachToComponent(BuoyancyRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		CameraBoom->SetRelativeTransform(SavedBoomRelativeTransform);
		CameraBoom->bUsePawnControlRotation = true;
		CameraBoom->TargetArmLength = SavedTargetArmLength;
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(SavedFollowCameraRelativeLocation);
			FollowCamera->SetRelativeRotation(SavedFollowCameraRelativeRotation);
		}
		PC->SetControlRotation(SavedControlRotation);

		UE_LOG(LogTemp, Log, TEXT("Switched back to follow camera."));
	}
}

void AShip::ResetToFollowCamera()
{
	if (bUsingFixedCamera)
	{
		bUsingFixedCamera = false;

		CameraBoom->AttachToComponent(BuoyancyRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		CameraBoom->SetRelativeTransform(SavedBoomRelativeTransform);
		CameraBoom->bUsePawnControlRotation = true;
		CameraBoom->TargetArmLength = SavedTargetArmLength;
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(SavedFollowCameraRelativeLocation);
			FollowCamera->SetRelativeRotation(SavedFollowCameraRelativeRotation);
		}

		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->SetControlRotation(SavedControlRotation);
		}
	}
}

void AShip::OnRep_RidingPlayer(APawn* OldRidingPlayer)
{
	UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer. OldRidingPlayer: %s, RidingPlayer: %s"), 
		OldRidingPlayer ? *OldRidingPlayer->GetName() : TEXT("Null"), 
		RidingPlayer ? *RidingPlayer->GetName() : TEXT("Null"));

	if (OldRidingPlayer && OldRidingPlayer != RidingPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Restoring old passenger collision and walking movement."));
		OldRidingPlayer->SetActorEnableCollision(true);
		if (ACharacter* Char = Cast<ACharacter>(OldRidingPlayer))
		{
			Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}

	if (RidingPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Disabling current passenger collision and movement."));
		RidingPlayer->SetActorEnableCollision(false);
		if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
		{
			Char->GetCharacterMovement()->DisableMovement();
			Char->GetCharacterMovement()->StopMovementImmediately();
		}
	}
}

void AShip::OnRep_Controller()
{
	Super::OnRep_Controller();

	if (Controller == nullptr)
	{
		if (CachedPlayerController)
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPlayerController->GetLocalPlayer()))
			{
				if (ShipInputMappingContext)
				{
					Subsystem->RemoveMappingContext(ShipInputMappingContext);
					UE_LOG(LogTemp, Log, TEXT("AShip: Removed ShipInputMappingContext in OnRep_Controller."));
				}
			}
			CachedPlayerController = nullptr;
		}
	}
	else
	{
		CachedPlayerController = Cast<APlayerController>(Controller);
		if (CachedPlayerController)
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPlayerController->GetLocalPlayer()))
			{
				if (ShipInputMappingContext)
				{
					Subsystem->AddMappingContext(ShipInputMappingContext, ShipInputPriority);
					UE_LOG(LogTemp, Log, TEXT("AShip: Added ShipInputMappingContext in OnRep_Controller."));
				}
			}
		}
	}
}


