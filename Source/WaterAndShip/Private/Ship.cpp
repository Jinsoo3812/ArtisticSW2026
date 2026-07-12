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
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "ShipAttributeSet.h"
#include "BaseGameplayTags.h"
#include "ShipPhysicsAsync.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "BuoyancyComponent.h"
#include "WaterBodyActor.h"
#include "EngineUtils.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "Chaos/PhysicsObject.h"
#include "Interfaces/IPhysicsComponent.h"





// Sets default values
AShip::AShip()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Buoyancy Root
	BuoyancyRoot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuoyancyRoot"));
	RootComponent = BuoyancyRoot;
	BuoyancyRoot->SetSimulatePhysics(true);
	BuoyancyRoot->SetCollisionProfileName(TEXT("PlayerShip"));

	Tags.AddUnique(TEXT("Player"));

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

	// Port Sea Boarding Component
	PortSeaBoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(TEXT("PortSeaBoardingInteractable"));
	PortSeaBoardingInteractable->SetupAttachment(BuoyancyRoot);
	PortSeaBoardingInteractable->SetCollisionProfileName(TEXT("Interactable"));

	PortSeaBoardingDestination = CreateDefaultSubobject<USceneComponent>(TEXT("PortSeaBoardingDestination"));
	PortSeaBoardingDestination->SetupAttachment(BuoyancyRoot);

	// Starboard Sea Boarding Component
	StarboardSeaBoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(TEXT("StarboardSeaBoardingInteractable"));
	StarboardSeaBoardingInteractable->SetupAttachment(BuoyancyRoot);
	StarboardSeaBoardingInteractable->SetCollisionProfileName(TEXT("Interactable"));

	StarboardSeaBoardingDestination = CreateDefaultSubobject<USceneComponent>(TEXT("StarboardSeaBoardingDestination"));
	StarboardSeaBoardingDestination->SetupAttachment(BuoyancyRoot);

	// Ability System Component
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Attribute Set
	AttributeSet = CreateDefaultSubobject<UShipAttributeSet>(TEXT("AttributeSet"));

	// Network Physics Component
	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction)
	{
		static const FName NetworkPhysicsComponentName(TEXT("NetworkPhysicsComponent"));
		NetworkPhysicsComponent = CreateDefaultSubobject<UNetworkPhysicsComponent>(NetworkPhysicsComponentName);
		NetworkPhysicsComponent->SetNetAddressable();
		SetPhysicsReplicationMode(EPhysicsReplicationMode::Resimulation);
	}

	bReplicates = true;
	SetReplicateMovement(true); // Resimulation 모드 작동을 위해 물리 무브먼트 복제 활성화
	bAlwaysRelevant = true;
}

// Called when the game starts or when spawned
void AShip::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	if (HasAuthority() && AttributeSet)
	{
		InitializeDefaultAttributes();
	}

	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetCollisionProfileName(TEXT("PlayerShip"));
		BuoyancyRoot->SetSimulatePhysics(true);
	}

	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction && NetworkPhysicsComponent)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysicsScene())
			{
				if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
				{
					ShipPhysicsAsync = Solver->CreateAndRegisterSimCallbackObject_External<FShipPhysicsAsync>();
					if (ShipPhysicsAsync)
					{
						if (BuoyancyRoot && BuoyancyRoot->GetBodyInstance())
						{
							ShipPhysicsAsync->SetPhysicsObject(reinterpret_cast<Chaos::FConstPhysicsObjectHandle>(BuoyancyRoot->GetBodyInstance()->GetPhysicsActorHandle()));
						}
						NetworkPhysicsComponent->CreateDataHistory(ShipPhysicsAsync);
					}
				}
			}
		}
	}

	if (UActorComponent* BuoyancyComp = GetComponentByClass(UBuoyancyComponent::StaticClass()))
	{
		BuoyancyComp->SetActive(false);
	}

	// Initialize replicated state to avoid teleporting to 0,0,0 on client initialization
	ReplicatedState.Location = GetActorLocation();
	ReplicatedState.Rotation = GetActorRotation();

	// 좌현 바다 승선 상호작용 바인딩
	if (PortSeaBoardingInteractable)
	{
		PortSeaBoardingInteractable->InitializeInteractable(
			FText::FromString(TEXT("배")),
			FText::FromString(TEXT("승선하기"))
		);
		PortSeaBoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShip::HandlePortSeaBoarding);
	}

	// 우현 바다 승선 상호작용 바인딩
	if (StarboardSeaBoardingInteractable)
	{
		StarboardSeaBoardingInteractable->InitializeInteractable(
			FText::FromString(TEXT("배")),
			FText::FromString(TEXT("승선하기"))
		);
		StarboardSeaBoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShip::HandleStarboardSeaBoarding);
	}
}

void AShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShipPhysicsAsync)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysicsScene())
			{
				if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
				{
					Solver->UnregisterAndFreeSimCallbackObject_External(ShipPhysicsAsync);
				}
			}
		}
		ShipPhysicsAsync = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ShipPhysicsAsync)
	{
		// 1. 조작 입력 데이터 마샬링 (Autonomous Proxy 및 Local Controller 전용)
		if (IsLocallyControlled())
		{
			if (FAsyncInputShip* AsyncInput = ShipPhysicsAsync->GetProducerInputData_External())
			{
				AsyncInput->MovementInput = CurrentMoveInput;
				AsyncInput->SteeringInput = CurrentTurnInput;
			}
		}

		// 2. 물리 틱용 기초 구조 정보 및 파도 파라미터 마샬링 (서버 & 클라이언트 로컬 물리 예측 공통)
		if (IsLocallyControlled() || HasAuthority())
		{
			if (FAsyncInputShip* AsyncInput = ShipPhysicsAsync->GetProducerInputData_External())
			{
				static bool bStaticDataInitialized = false;
				if (!bStaticDataInitialized)
				{
					bStaticDataInitialized = true;

					// 폰툰 오프셋 수집
					if (UBuoyancyComponent* BuoyancyComp = Cast<UBuoyancyComponent>(GetComponentByClass(UBuoyancyComponent::StaticClass())))
					{
						for (const FSphericalPontoon& Pontoon : BuoyancyComp->BuoyancyData.Pontoons)
						{
							AsyncInput->PontoonOffsets.Add(Pontoon.RelativeLocation);
						}
					}

					// 결정론적 파고 계산을 위한 Gerstner Waves 자산 데이터 수집
					for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
					{
						if (AWaterBody* WaterBody = *It)
						{
							if (UGerstnerWaterWaves* GerstnerAsset = Cast<UGerstnerWaterWaves>(WaterBody->GetWaterWaves()))
							{
								AsyncInput->GerstnerWaves = GerstnerAsset->GetGerstnerWaves();
								break;
							}
						}
					}

					// 기타 물리/부력 매개변수 전송
					AsyncInput->GravityZ = GetWorld()->GetGravityZ();
					AsyncInput->LateralDrag = LateralDragCoefficient;
					AsyncInput->ForwardForceValue = ForwardForce;
					AsyncInput->TurnTorqueValue = TurnTorque;
					
					float SpeedMult = 1.0f;
					if (AttributeSet)
					{
						SpeedMult = AttributeSet->GetShipSpeedMultiplier();
					}
					AsyncInput->SpeedMultiplier = SpeedMult;

					// 부력 댐핑 및 반경 설정
					AsyncInput->BuoyancyRadius = 150.f; // 폰툰 기본 반경
					AsyncInput->BuoyancyForceMultiplier = 1.3f;
					AsyncInput->WaterDamping = 3.0f;
				}
			}
		}
	}

	if (HasAuthority())
	{
		ReplicatedState.Location = GetActorLocation();
		ReplicatedState.Rotation = GetActorRotation();
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
	UE_LOG(LogTemp, Log, TEXT("AShip::Board - [SERVER] Entered. PlayerPawn: %s, HasAuthority: %s, RidingPlayer: %s"),
		PlayerPawn ? *PlayerPawn->GetName() : TEXT("None"),
		HasAuthority() ? TEXT("YES") : TEXT("NO"),
		RidingPlayer ? *RidingPlayer->GetName() : TEXT("None"));

	if (!HasAuthority()) return;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: PlayerPawn is null!"));
		return;
	}
	if (RidingPlayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: Ship is already being ridden by %s!"), *RidingPlayer->GetName());
		return;
	}

	APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController());
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: PlayerPawn has no PlayerController! Pawn: %s, Controller: %s"),
			*PlayerPawn->GetName(),
			PlayerPawn->GetController() ? *PlayerPawn->GetController()->GetName() : TEXT("None"));
		return;
	}

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
	CurrentMoveInput = MoveValue;

	if (!HasAuthority())
	{
		ServerMove(MoveValue);
	}
}

void AShip::ServerMove_Implementation(float MoveValue)
{
	CurrentMoveInput = MoveValue;
}

void AShip::ApplyForwardForce(float MoveValue)
{
	// 비동기 물리 스레드(FShipPhysicsAsync)에서 물리 힘이 연산되므로 빈 함수로 둡니다.
}

void AShip::ShipTurn(const FInputActionValue& Value)
{
	const float TurnValue = Value.Get<float>();
	CurrentTurnInput = TurnValue;

	if (!HasAuthority())
	{
		ServerTurn(TurnValue);
	}
}

void AShip::ServerTurn_Implementation(float TurnValue)
{
	CurrentTurnInput = TurnValue;
}

void AShip::ApplyTurnTorque(float TurnValue)
{
	// 비동기 물리 스레드(FShipPhysicsAsync)에서 물리 힘이 연산되므로 빈 함수로 둡니다.
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
	// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer. OldRidingPlayer: %s, RidingPlayer: %s"), 
	// 	OldRidingPlayer ? *OldRidingPlayer->GetName() : TEXT("Null"), 
	// 	RidingPlayer ? *RidingPlayer->GetName() : TEXT("Null"));

	APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	if (OldRidingPlayer && OldRidingPlayer != RidingPlayer)
	{
		// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Restoring old passenger collision and walking movement."));
		OldRidingPlayer->SetActorEnableCollision(true);
		if (ACharacter* Char = Cast<ACharacter>(OldRidingPlayer))
		{
			Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}

		if (LocalPC && LocalPC->IsLocalController())
		{
			LocalPC->HiddenActors.Remove(OldRidingPlayer);
		}
	}

	if (RidingPlayer)
	{
		// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Disabling current passenger collision and movement."));
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

void AShip::OnRep_Controller()
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
				if (ShipInputMappingContext)
				{
					Subsystem->RemoveMappingContext(ShipInputMappingContext);
					// UE_LOG(LogTemp, Log, TEXT("AShip: Removed ShipInputMappingContext in OnRep_Controller."));
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
					// UE_LOG(LogTemp, Log, TEXT("AShip: Added ShipInputMappingContext in OnRep_Controller."));
				}
			}
		}
	}
}

UAbilitySystemComponent* AShip::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShip::InitializeDefaultAttributes()
{
	if (!HasAuthority() || !AttributeSet) return;

	if (ShipStatTable && !ShipStatRowName.IsNone())
	{
		static const FString ContextString(TEXT("Ship Stat Table Context"));
		FShipStatRow* StatRow = ShipStatTable->FindRow<FShipStatRow>(ShipStatRowName, ContextString);
		if (StatRow)
		{
			AttributeSet->InitHealth(StatRow->MaxHealth);
			AttributeSet->InitMaxHealth(StatRow->MaxHealth);
			AttributeSet->InitMoveSpeed(1.0f); // 캐릭터 기본 MoveSpeed는 1.0f로 고정 유지
			AttributeSet->InitShipSpeedMultiplier(StatRow->ShipSpeedMultiplier);
			AttributeSet->InitCannonDamage(StatRow->CannonDamage);
			AttributeSet->InitCannonFireCooldown(StatRow->CannonFireCooldown);
			AttributeSet->InitCannonballSpeed(StatRow->CannonballSpeed);

			UE_LOG(LogTemp, Log, TEXT("AShip: Successfully initialized attributes from DataTable Row [%s]."), *ShipStatRowName.ToString());
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AShip: Failed to find DataTable Row [%s] in ShipStatTable."), *ShipStatRowName.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("AShip: ShipStatTable or ShipStatRowName is not set. Initializing with default fallback stats."));
	}

	// Fallback 기본값 설정
	AttributeSet->InitHealth(100.f);
	AttributeSet->InitMaxHealth(100.f);
	AttributeSet->InitMoveSpeed(1.f);
	AttributeSet->InitShipSpeedMultiplier(1.f);
	AttributeSet->InitCannonDamage(20.f);
	AttributeSet->InitCannonFireCooldown(2.f);
	AttributeSet->InitCannonballSpeed(3000.f);
}

void AShip::HandlePortSeaBoarding(AActor* Interactor)
{
	if (!Interactor) return;

	if (HasAuthority())
	{
		FVector DestinationLoc = PortSeaBoardingDestination ? PortSeaBoardingDestination->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, 200.f);
		FRotator DestinationRot = PortSeaBoardingDestination ? PortSeaBoardingDestination->GetComponentRotation() : GetActorRotation();

		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
				UE_LOG(LogTemp, Log, TEXT("AShip::HandlePortSeaBoarding - Character movement mode set to MOVE_Walking."));
			}
		}

		Interactor->TeleportTo(DestinationLoc, DestinationRot);

		UE_LOG(LogTemp, Log, TEXT("AShip::HandlePortSeaBoarding - Teleported %s to port boarding destination: %s"), 
			*Interactor->GetName(), *DestinationLoc.ToString());
	}
}

void AShip::HandleStarboardSeaBoarding(AActor* Interactor)
{
	if (!Interactor) return;

	if (HasAuthority())
	{
		FVector DestinationLoc = StarboardSeaBoardingDestination ? StarboardSeaBoardingDestination->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, 200.f);
		FRotator DestinationRot = StarboardSeaBoardingDestination ? StarboardSeaBoardingDestination->GetComponentRotation() : GetActorRotation();

		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
				UE_LOG(LogTemp, Log, TEXT("AShip::HandleStarboardSeaBoarding - Character movement mode set to MOVE_Walking."));
			}
		}

		Interactor->TeleportTo(DestinationLoc, DestinationRot);

		UE_LOG(LogTemp, Log, TEXT("AShip::HandleStarboardSeaBoarding - Teleported %s to starboard boarding destination: %s"), 
			*Interactor->GetName(), *DestinationLoc.ToString());
	}
}


