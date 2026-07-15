// Copyright Epic Games, Inc. All Rights Reserved.


#include "ArtisticSW2026PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "ArtisticSW2026.h"
#include "Widgets/Input/SVirtualJoystick.h"

void AArtisticSW2026PlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	// [Antigravity] 데디케이트 서버 크래시 방지: IsLocalPlayerController()를 먼저 체크하여 서버에서 ShouldUseTouchControls()가 호출되는 것을 막음
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogArtisticSW2026, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AArtisticSW2026PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool AArtisticSW2026PlayerController::ShouldUseTouchControls() const
{
	// [Antigravity] 데디케이트 서버(UI/Slate가 없는 환경)에서 SVirtualJoystick 관련 함수 호출 시 크래시가 발생하는 것을 방지
	if (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
