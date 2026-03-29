// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerController.h"
#include "BasePlayer.h"
#include "UI/PlayerHUDWidget.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "BaseGameplayTags.h"


void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// [클라/로컬]
	if (IsLocalPlayerController())
	{
		// UI IMC 등록
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* Context : UIIMC)
			{
				Subsystem->AddMappingContext(Context, UIIMCPriority);
			}
		}
	}

	if (IsLocalController() && PlayerHUDWidgetClass)
	{
		PlayerHUDWidget = CreateWidget<UPlayerHUDWidget>(this, PlayerHUDWidgetClass);
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->AddToViewport();
			PlayerHUDWidget->SetInventoryVisible(false);
		}
	}
}

void ABasePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (UIInputConfig)
		{
			for (const FKeyInputAction& Action : UIInputConfig->KeyInputActions)
			{
				if (Action.InputAction && Action.KeyTag.IsValid())
				{
					// Player를 거치지 않고, PC 자신의 OnUIInputPressed 함수로 직행
					EnhancedInput->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayerController::OnUIInputPressed, Action.KeyTag);
					UE_LOG(LogTemp, Log, TEXT("Bound UI input: %s"), *Action.KeyTag.ToString());	
				}
			}
		}
	}
}

void ABasePlayerController::OnUIInputPressed(FGameplayTag InputTag)
{
	// [클라/로컬]
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	if (InputTag.MatchesTagExact(Key_UI_I))
	{
		const bool bOpen = !PlayerHUDWidget->IsInventoryVisible();
		PlayerHUDWidget->SetInventoryVisible(bOpen);
		ApplyInventoryInputMode(bOpen);
	}
}

void ABasePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindHUDToCurrentPlayer();
}

void ABasePlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	BindHUDToCurrentPlayer();
}

void ABasePlayerController::BindHUDToCurrentPlayer()
{
	// [클라/로컬] HUD 위젯은 로컬 플레이어에게만
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	if (ABasePlayer* BasePlayer = Cast<ABasePlayer>(GetPawn()))
	{
		PlayerHUDWidget->InitializeForPlayer(BasePlayer);
	}
}

void ABasePlayerController::ToggleInventory()
{
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	const bool bOpen = !PlayerHUDWidget->IsInventoryVisible();
	PlayerHUDWidget->SetInventoryVisible(bOpen);
	ApplyInventoryInputMode(bOpen);
}

void ABasePlayerController::ApplyInventoryInputMode(bool bOpen)
{
	bShowMouseCursor = bOpen;

	if (bOpen)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		SetIgnoreLookInput(true);
		SetIgnoreMoveInput(true);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);

		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
	}
}