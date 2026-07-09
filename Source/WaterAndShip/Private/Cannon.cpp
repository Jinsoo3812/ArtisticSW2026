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
#include "ShipAttributeSet.h"
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

	// Interactable Component (Ship과 동일 패턴 - BeginPlay에서 바인딩하지 않음)
	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(RootComponent);
	InteractableComponent->SetCollisionProfileName(TEXT("Interactable"));

	bReplicates = true;
	SetReplicateMovement(false); // We replicate rotations manually via AimRotation
}

void ACannon::BeginPlay()
{
	Super::BeginPlay();

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

	// Ship의 SetupPlayerInputComponent와 동일 패턴: CachedPlayerController 캐싱
	CachedPlayerController = Cast<APlayerController>(GetController());

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (CannonLookAction)
		{
			EnhancedInput->BindAction(CannonLookAction, ETriggerEvent::Triggered, this, &ACannon::HandleLook);
		}
		if (CannonFireAction)
		{
			EnhancedInput->BindAction(CannonFireAction, ETriggerEvent::Started, this, &ACannon::HandleFire);
		}
		if (CannonExitAction)
		{
			EnhancedInput->BindAction(CannonExitAction, ETriggerEvent::Started, this, &ACannon::HandleExit);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ACannon::SetupPlayerInputComponent - Failed to find Enhanced Input Component."));
	}
}

void ACannon::UnPossessed()
{
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

// Ship의 Board()와 완전히 동일한 패턴
void ACannon::Board(APawn* PlayerPawn)
{
	if (!HasAuthority()) return;
	if (!PlayerPawn || RidingPlayer) return;

	APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController());
	if (!PC) return;

	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] Board initiated by player pawn %s. Cannon location: %s, Player location: %s"), *PlayerPawn->GetName(), *GetActorLocation().ToString(), *PlayerPawn->GetActorLocation().ToString());

	RidingPlayer = PlayerPawn;

	// Disable player collision
	RidingPlayer->SetActorEnableCollision(false);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->DisableMovement();
		Char->GetCharacterMovement()->StopMovementImmediately();
	}

	// Disable movement replication while on the cannon to prevent jittering
	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] Board - Player bReplicateMovement before disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));
	RidingPlayer->SetReplicateMovement(false);
	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] Board - Player bReplicateMovement after disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Attach to BaseMesh directly without welding physics bodies to avoid physics conflicts
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
	RidingPlayer->AttachToComponent(BaseMesh, AttachmentRules);
	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] Board - Player attached to BaseMesh. Relative location: %s, relative rotation: %s"), 
		*RidingPlayer->GetRootComponent()->GetRelativeLocation().ToString(), 
		*RidingPlayer->GetRootComponent()->GetRelativeRotation().ToString());

	// Possess cannon pawn (Ship의 Board와 동일)
	PC->Possess(this);
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
	FireCannon();
}

bool ACannon::FireCannon()
{
	if (!bCanFire) return false;

	AShip* MyShip = GetOwningShip();
	UE_LOG(LogTemp, Warning, TEXT("ACannon::FireCannon - Fire! Cannon: %s, Ship: %s"), *GetName(), MyShip ? *MyShip->GetName() : TEXT("None"));

	bCanFire = false;

	// 배의 스탯 초기값 (Fallback)
	float TargetCooldown = FireCooldown;
	float TargetDamage = 10.0f;
	float TargetSpeed = FireVelocity;

	// 배가 존재하면 배의 GAS Attribute에서 실시간으로 대포 스탯들을 긁어옴
	if (MyShip)
	{
		if (UAbilitySystemComponent* ShipASC = MyShip->GetAbilitySystemComponent())
		{
			TargetCooldown = ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonFireCooldownAttribute());
			TargetDamage = ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute());
			TargetSpeed = ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonballSpeedAttribute());
		}
	}

	// 스탯 기반의 발사 쿨타임 타이머 작동
	GetWorldTimerManager().SetTimer(CooldownTimerHandle, this, &ACannon::ResetCooldown, TargetCooldown, false);

	FVector MuzzleLocation = BarrelMesh ? BarrelMesh->GetComponentLocation() + BarrelMesh->GetForwardVector() * 200.0f : GetActorLocation();
	FRotator LaunchRotation = BarrelMesh ? BarrelMesh->GetComponentRotation() : GetActorRotation();

	if (HasAuthority())
	{
		SpawnCannonball(MuzzleLocation, LaunchRotation, TargetDamage, TargetSpeed);
	}
	else
	{
		ServerFire(MuzzleLocation, LaunchRotation, TargetDamage, TargetSpeed);
	}

	return true;
}

void ACannon::SetAIAimRotation(float NewPitch, float NewYaw)
{
	if (HasAuthority())
	{
		AimRotation.Pitch = FMath::Clamp(NewPitch, MinPitch, MaxPitch);
		AimRotation.Yaw = FMath::Clamp(NewYaw, -MaxYawOffset, MaxYawOffset);
	}
}

void ACannon::HandleExit(const FInputActionValue& Value)
{
	ServerExit();
}

// Ship의 Disembark()와 동일한 패턴
void ACannon::ExitAimMode()
{
	if (!HasAuthority()) return;
	if (!RidingPlayer) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] ExitAimMode initiated. Player pawn: %s"), *RidingPlayer->GetName());

	// Detach player preserving their current world position on the cannon
	RidingPlayer->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] ExitAimMode - Detached player. World location: %s"), *RidingPlayer->GetActorLocation().ToString());

	RidingPlayer->SetActorEnableCollision(true);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	// Restore movement replication on exit
	RidingPlayer->SetReplicateMovement(true);
	UE_LOG(LogTemp, Log, TEXT("ACannon: [SERVER] ExitAimMode - Player bReplicateMovement after enable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Return possession to player character (Ship의 Disembark와 동일)
	PC->Possess(RidingPlayer);

	RidingPlayer = nullptr;
}

void ACannon::ForceExit()
{
	if (HasAuthority() && RidingPlayer)
	{
		ExitAimMode();
	}
}

void ACannon::ServerFire_Implementation(FVector MuzzleLocation, FRotator LaunchRotation, float Damage, float Speed)
{
	SpawnCannonball(MuzzleLocation, LaunchRotation, Damage, Speed);
}

void ACannon::SpawnCannonball(FVector MuzzleLocation, FRotator LaunchRotation, float Damage, float Speed)
{
	if (!HasAuthority()) return;

	if (!CannonballClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ACannon::SpawnCannonball - CannonballClass is null. Please assign it in the editor."));
		return;
	}

	AShip* OwningShip = GetOwningShip();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningShip ? Cast<AActor>(OwningShip) : Cast<AActor>(this);
	SpawnParams.Instigator = RidingPlayer; // The player who controls the cannon (might be null for AI)
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedProjectile = GetWorld()->SpawnActor<AActor>(CannonballClass, MuzzleLocation, LaunchRotation, SpawnParams);
	if (SpawnedProjectile)
	{
		if (ACannonball* Projectile = Cast<ACannonball>(SpawnedProjectile))
		{
			Projectile->InitializeProjectile(OwningShip, Damage, Speed);
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

// Ship의 OnRep_RidingPlayer()와 동일 패턴
void ACannon::OnRep_RidingPlayer(APawn* OldPlayer)
{
	UE_LOG(LogTemp, Log, TEXT("ACannon: [CLIENT] OnRep_RidingPlayer. OldPlayer: %s, RidingPlayer: %s"), 
		OldPlayer ? *OldPlayer->GetName() : TEXT("Null"), 
		RidingPlayer ? *RidingPlayer->GetName() : TEXT("Null"));

	APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	if (OldPlayer && OldPlayer != RidingPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("ACannon: [CLIENT] OnRep_RidingPlayer - Restoring old passenger collision and walking movement."));
		OldPlayer->SetActorEnableCollision(true);
		if (ACharacter* Char = Cast<ACharacter>(OldPlayer))
		{
			Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}

		if (LocalPC && LocalPC->IsLocalController())
		{
			LocalPC->HiddenActors.Remove(OldPlayer);
		}
	}

	if (RidingPlayer)
	{
		UE_LOG(LogTemp, Log, TEXT("ACannon: [CLIENT] OnRep_RidingPlayer - Disabling current passenger collision and movement."));
		RidingPlayer->SetActorEnableCollision(false);
		if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
		{
			Char->GetCharacterMovement()->DisableMovement();
			Char->GetCharacterMovement()->StopMovementImmediately();
		}

		if (LocalPC && LocalPC->IsLocalController())
		{
			LocalPC->HiddenActors.AddUnique(RidingPlayer);
		}
	}
}

// Ship의 OnRep_Controller()와 완전히 동일한 패턴
void ACannon::OnRep_Controller()
{
	Super::OnRep_Controller();

	if (Controller == nullptr)
	{
		APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (LocalPC && LocalPC->IsLocalController() && RidingPlayer)
		{
			LocalPC->HiddenActors.Remove(RidingPlayer);
		}

		if (CachedPlayerController)
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPlayerController->GetLocalPlayer()))
			{
				if (CannonInputMappingContext)
				{
					Subsystem->RemoveMappingContext(CannonInputMappingContext);
					UE_LOG(LogTemp, Log, TEXT("ACannon: Removed CannonInputMappingContext in OnRep_Controller."));
				}
			}

			// UI 정리
			if (AimWidgetInstance)
			{
				AimWidgetInstance->RemoveFromParent();
				AimWidgetInstance = nullptr;
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
				if (CannonInputMappingContext)
				{
					Subsystem->AddMappingContext(CannonInputMappingContext, CannonInputPriority);
					UE_LOG(LogTemp, Log, TEXT("ACannon: Added CannonInputMappingContext in OnRep_Controller."));
				}
			}

			// UI 생성
			if (AimWidgetClass && !AimWidgetInstance)
			{
				AimWidgetInstance = CreateWidget<UUserWidget>(CachedPlayerController, AimWidgetClass);
				if (AimWidgetInstance)
				{
					AimWidgetInstance->AddToViewport();
				}
			}
		}
	}
}
