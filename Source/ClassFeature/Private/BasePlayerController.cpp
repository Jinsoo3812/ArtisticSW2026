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
#include "Attacker/AttackerComponent.h"
#include "Crafter/CrafterComponent.h"


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
					EnhancedInput->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayerController::OnUIInputPressed, Action.KeyTag);
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
		// Attacker는 인벤토리 사용불가.
		if (const APawn* ControlledPawn = GetPawn())
		{
			if (ControlledPawn->FindComponentByClass<UAttackerComponent>())
			{
				return;
			}
		}
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

	// Attacker는 인벤토리 사용 불가.
	if (const APawn* ControlledPawn = GetPawn())
	{
		if (ControlledPawn->FindComponentByClass<UAttackerComponent>())
		{
			return;
		}
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
		// 게임 입력, UI 입력 모두 받을 수 있는 InputMode
		FInputModeGameAndUI InputMode;
		// 클릭 및 드래그 할 때 커서 숨기지 않음
		InputMode.SetHideCursorDuringCapture(false);
		// 게임 화면 안에 마우스 가두지 않음
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		// playercontroller에 적용
		SetInputMode(InputMode);

		// 캐릭터나 카메라 회전 막기 true
		SetIgnoreLookInput(true);
		// 이동 입력 가능하게 설정
		SetIgnoreMoveInput(false);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);

		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
	}
}