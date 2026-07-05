// Fill out your copyright notice in the Description page of Project Settings.

#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "InteractableComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/UserWidget.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "Kismet/GameplayStatics.h"
#include "Cannonball.h"

ACannon::ACannon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root Scene Component
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	RootComponent->SetMobility(EComponentMobility::Movable);

	// Base Mesh (Yaw rotation)
	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(RootComponent);
	BaseMesh->SetMobility(EComponentMobility::Movable);
	BaseMesh->SetCollisionProfileName(TEXT("NoCollision"));

	// Barrel Mesh (Pitch rotation)
	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(BaseMesh);
	BarrelMesh->SetMobility(EComponentMobility::Movable);
	BarrelMesh->SetCollisionProfileName(TEXT("NoCollision"));

	// Aim Camera
	AimCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("AimCamera"));
	AimCamera->SetupAttachment(BarrelMesh);
	AimCamera->SetMobility(EComponentMobility::Movable);
	AimCamera->bUsePawnControlRotation = false; // We drive the rotation manually

	// Interactable Component
	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(RootComponent);
	InteractableComponent->SetCollisionProfileName(TEXT("Interactable"));

	bReplicates = true;
	SetReplicateMovement(false); // We replicate rotations manually via AimRotation
}

void ACannon::BeginPlay()
{
	Super::BeginPlay();

	// Bind interact event
	if (InteractableComponent)
	{
		InteractableComponent->OnInteracted.AddUniqueDynamic(this, &ACannon::OnInteracted);
	}

	// Capture initial rotations
	if (BaseMesh)
	{
		InitialBaseRotation = BaseMesh->GetRelativeRotation();
	}
	if (BarrelMesh)
	{
		InitialBarrelRotation = BarrelMesh->GetRelativeRotation();
	}
}

void ACannon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Apply rotation to meshes
	if (BaseMesh)
	{
		FRotator TargetBaseRot = InitialBaseRotation;
		TargetBaseRot.Yaw += AimRotation.Yaw;
		BaseMesh->SetRelativeRotation(TargetBaseRot);
	}

	if (BarrelMesh)
	{
		FRotator TargetBarrelRot = InitialBarrelRotation;
		TargetBarrelRot.Pitch += AimRotation.Pitch;
		BarrelMesh->SetRelativeRotation(TargetBarrelRot);
	}
}

void ACannon::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogTemp, Log, TEXT("ACannon::SetupPlayerInputComponent Called!"));

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (CannonLookAction)
		{
			EnhancedInput->BindAction(CannonLookAction, ETriggerEvent::Triggered, this, &ACannon::HandleLook);
			UE_LOG(LogTemp, Log, TEXT("ACannon::SetupPlayerInputComponent - Bound LookAction"));
		}
		if (CannonFireAction)
		{
			EnhancedInput->BindAction(CannonFireAction, ETriggerEvent::Started, this, &ACannon::HandleFire);
			UE_LOG(LogTemp, Log, TEXT("ACannon::SetupPlayerInputComponent - Bound FireAction"));
		}
		if (CannonExitAction)
		{
			EnhancedInput->BindAction(CannonExitAction, ETriggerEvent::Started, this, &ACannon::HandleExit);
			UE_LOG(LogTemp, Log, TEXT("ACannon::SetupPlayerInputComponent - Bound ExitAction"));
		}
	}
}

void ACannon::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	CachedPC = Cast<APlayerController>(NewController);

	// Explicitly enable input for this pawn
	EnableInput(CachedPC);

	if (CachedPC && CachedPC->IsLocalController())
	{
		// Clients or Listen Server Local
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPC->GetLocalPlayer()))
		{
			if (CannonInputMappingContext)
			{
				Subsystem->AddMappingContext(CannonInputMappingContext, CannonInputPriority);
			}
		}

		if (AimWidgetClass && !AimWidgetInstance)
		{
			AimWidgetInstance = CreateWidget<UUserWidget>(CachedPC, AimWidgetClass);
			if (AimWidgetInstance)
			{
				AimWidgetInstance->AddToViewport();
			}
		}
	}
}

void ACannon::UnPossessed()
{
	if (CachedPC)
	{
		if (CachedPC->IsLocalController())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPC->GetLocalPlayer()))
			{
				if (CannonInputMappingContext)
				{
					Subsystem->RemoveMappingContext(CannonInputMappingContext);
				}
			}

			if (AimWidgetInstance)
			{
				AimWidgetInstance->RemoveFromParent();
				AimWidgetInstance = nullptr;
			}
		}
	}

	CachedPC = nullptr;
	Super::UnPossessed();
}

AShip* ACannon::GetOwningShip() const
{
	// 1. Try get attach parent
	AShip* MyShip = Cast<AShip>(GetAttachParentActor());
	if (MyShip) return MyShip;

	// 2. Try get parent actor (if child actor component)
	MyShip = Cast<AShip>(GetParentActor());
	if (MyShip) return MyShip;

	// 3. Try get owner
	MyShip = Cast<AShip>(GetOwner());
	if (MyShip) return MyShip;

	// 4. Trace up attach hierarchy
	AActor* CurrentParent = GetAttachParentActor();
	while (CurrentParent)
	{
		MyShip = Cast<AShip>(CurrentParent);
		if (MyShip) return MyShip;
		CurrentParent = CurrentParent->GetAttachParentActor();
	}

	return nullptr;
}

void ACannon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACannon, RidingPlayer);
	DOREPLIFETIME(ACannon, AimRotation);
}

void ACannon::OnInteracted(AActor* Interactor)
{
	if (!HasAuthority()) return;

	APawn* InteractingPawn = Cast<APawn>(Interactor);
	if (!InteractingPawn || RidingPlayer) return;

	RidingPlayer = InteractingPawn;
	EnterAimMode(RidingPlayer.Get());
}

void ACannon::EnterAimMode(APawn* InPlayer)
{
	if (!HasAuthority() || !InPlayer) return;

	// Disable collision and movement
	InPlayer->SetActorEnableCollision(false);

	if (ACharacter* Character = Cast<ACharacter>(InPlayer))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->DisableMovement();
			MoveComp->StopMovementImmediately();
		}
	}

	// Disable movement replication during Cannon ride
	InPlayer->SetReplicateMovement(false);

	// Attach Player to base mesh (Seat position)
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
	InPlayer->AttachToComponent(BaseMesh, AttachmentRules);

	// Possess Cannon Pawn
	AController* OldController = InPlayer->GetController();
	if (OldController)
	{
		OldController->Possess(this);
	}
}

void ACannon::HandleLook(const FInputActionValue& Value)
{
	const FVector2D LookValue = Value.Get<FVector2D>();

	if (HasAuthority())
	{
		float NewPitch = FMath::Clamp(AimRotation.Pitch + LookValue.Y, MinPitch, MaxPitch);
		float NewYaw = FMath::Clamp(AimRotation.Yaw + LookValue.X, -MaxYawOffset, MaxYawOffset);
		AimRotation.Pitch = NewPitch;
		AimRotation.Yaw = NewYaw;
	}
	else
	{
		// Predict locally
		float NewPitch = FMath::Clamp(AimRotation.Pitch + LookValue.Y, MinPitch, MaxPitch);
		float NewYaw = FMath::Clamp(AimRotation.Yaw + LookValue.X, -MaxYawOffset, MaxYawOffset);
		AimRotation.Pitch = NewPitch;
		AimRotation.Yaw = NewYaw;

		ServerUpdateAim(NewPitch, NewYaw);
	}
}

void ACannon::HandleFire(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Warning, TEXT("ACannon::HandleFire Called! CanFire: %d"), bCanFire);

	if (!bCanFire) return;

	// Cooldown start
	bCanFire = false;
	GetWorldTimerManager().SetTimer(CooldownTimerHandle, this, &ACannon::ResetCooldown, FireCooldown, false);

	FVector MuzzleLocation = BarrelMesh ? BarrelMesh->GetComponentLocation() + BarrelMesh->GetForwardVector() * 200.0f : GetActorLocation();
	FRotator LaunchRotation = BarrelMesh ? BarrelMesh->GetComponentRotation() : GetActorRotation();

	// Read ship attack power from ship GAS
	float Damage = 10.0f;
	if (AShip* MyShip = GetOwningShip())
	{
		if (UAbilitySystemComponent* ShipASC = MyShip->GetAbilitySystemComponent())
		{
			Damage = ShipASC->GetNumericAttribute(UBaseAttributeSet::GetAttackPowerAttribute());
		}
	}

	ServerFire(MuzzleLocation, LaunchRotation, Damage);
}

void ACannon::HandleExit(const FInputActionValue& Value)
{
	if (HasAuthority())
	{
		ExitAimMode();
	}
	else
	{
		ServerExit();
	}
}

void ACannon::ExitAimMode()
{
	if (!HasAuthority() || !RidingPlayer) return;

	APawn* PlayerPawn = RidingPlayer;
	RidingPlayer = nullptr;

	// Detach Player
	PlayerPawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PlayerPawn->SetActorEnableCollision(true);

	if (ACharacter* Character = Cast<ACharacter>(PlayerPawn))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}

	// Restore movement replication
	PlayerPawn->SetReplicateMovement(true);

	// Re-possess player pawn
	AController* PC = GetController();
	if (PC)
	{
		PC->Possess(PlayerPawn);
	}
}

void ACannon::ForceExit()
{
	if (HasAuthority() && RidingPlayer)
	{
		ExitAimMode();
	}
}

void ACannon::ServerFire_Implementation(FVector MuzzleLocation, FRotator LaunchRotation, float Damage)
{
	if (!CannonballClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ACannon::ServerFire - CannonballClass is null. Please assign it in the editor."));
		return;
	}

	AShip* OwningShip = GetOwningShip();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningShip ? Cast<AActor>(OwningShip) : Cast<AActor>(this);
	SpawnParams.Instigator = RidingPlayer; // The player who controls the cannon
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedProjectile = GetWorld()->SpawnActor<AActor>(CannonballClass, MuzzleLocation, LaunchRotation, SpawnParams);
	if (SpawnedProjectile)
	{
		if (ACannonball* Projectile = Cast<ACannonball>(SpawnedProjectile))
		{
			Projectile->InitializeProjectile(OwningShip, Damage);
		}
	}
}

void ACannon::ServerUpdateAim_Implementation(float NewPitch, float NewYaw)
{
	AimRotation.Pitch = NewPitch;
	AimRotation.Yaw = NewYaw;
}

void ACannon::ServerExit_Implementation()
{
	ExitAimMode();
}

void ACannon::ResetCooldown()
{
	bCanFire = true;
}

void ACannon::OnRep_AimRotation()
{
	// mesh rotation is handled in Tick
}

void ACannon::OnRep_RidingPlayer(APawn* OldPlayer)
{
	if (OldPlayer && OldPlayer != RidingPlayer)
	{
		OldPlayer->SetActorEnableCollision(true);

		if (ACharacter* Character = Cast<ACharacter>(OldPlayer))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
			}
		}
	}

	if (RidingPlayer)
	{
		RidingPlayer->SetActorEnableCollision(false);

		if (ACharacter* Character = Cast<ACharacter>(RidingPlayer))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->DisableMovement();
				MoveComp->StopMovementImmediately();
			}
		}
	}
}

void ACannon::OnRep_Controller()
{
	Super::OnRep_Controller();

	CachedPC = Cast<APlayerController>(GetController());

	if (CachedPC == nullptr)
	{
		// Clean up UI/IMC locally when unpossessed
		if (APlayerController* LocalPC = Cast<APlayerController>(GEngine->GetFirstLocalPlayerController(GetWorld())))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPC->GetLocalPlayer()))
			{
				if (CannonInputMappingContext)
				{
					Subsystem->RemoveMappingContext(CannonInputMappingContext);
				}
			}
		}

		if (AimWidgetInstance)
		{
			AimWidgetInstance->RemoveFromParent();
			AimWidgetInstance = nullptr;
		}
	}
	else if (CachedPC->IsLocalController())
	{
		// Explicitly enable input for this pawn locally
		EnableInput(CachedPC);

		// Apply local setup
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPC->GetLocalPlayer()))
		{
			if (CannonInputMappingContext)
			{
				Subsystem->AddMappingContext(CannonInputMappingContext, CannonInputPriority);
			}
		}

		if (AimWidgetClass && !AimWidgetInstance)
		{
			AimWidgetInstance = CreateWidget<UUserWidget>(CachedPC, AimWidgetClass);
			if (AimWidgetInstance)
			{
				AimWidgetInstance->AddToViewport();
			}
		}
	}
}
