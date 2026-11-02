#include "Recovery/StuckRecoveryInputComponent.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Recovery/StuckRecoveryComponent.h"
#include "Recovery/StuckRecoveryWidget.h"

UStuckRecoveryInputComponent::UStuckRecoveryInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	GuideWidgetClass = UStuckRecoveryWidget::StaticClass();
}

void UStuckRecoveryInputComponent::BeginPlay()
{
	Super::BeginPlay();
	Recovery = GetOwner()->FindComponentByClass<UStuckRecoveryComponent>();
	if (Recovery.IsValid()) Recovery->OnRecoveryResult.AddDynamic(this, &ThisClass::HandleRecoveryResult);
}

void UStuckRecoveryInputComponent::BindInput(UEnhancedInputComponent* Input)
{
	RemoveBindings();
	if (!Input || !EscapeAction) return;
	BoundInput = Input;
	BindingHandles.Add(Input->BindAction(EscapeAction, ETriggerEvent::Started, this, &ThisClass::Pressed).GetHandle());
	BindingHandles.Add(Input->BindAction(EscapeAction, ETriggerEvent::Completed, this, &ThisClass::Released).GetHandle());
	BindingHandles.Add(Input->BindAction(EscapeAction, ETriggerEvent::Canceled, this, &ThisClass::Released).GetHandle());
}

void UStuckRecoveryInputComponent::RemoveBindings()
{
	Released();
	if (BoundInput.IsValid())
		for (uint32 Handle : BindingHandles) BoundInput->RemoveBindingByHandle(Handle);
	BoundInput.Reset();
	BindingHandles.Empty();
}

void UStuckRecoveryInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RemoveBindings();
	if (Recovery.IsValid()) Recovery->OnRecoveryResult.RemoveDynamic(this, &ThisClass::HandleRecoveryResult);
	if (Guide) { Guide->RemoveFromParent(); Guide = nullptr; }
	Super::EndPlay(Reason);
}

void UStuckRecoveryInputComponent::Pressed()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !Recovery.IsValid() || !Recovery->IsEscapeAvailable() || !Character->IsLocallyControlled()
		|| !Controller || Controller->IsMoveInputIgnored()) return;
	bInputAccepted = true;
	Recovery->SetRecoveryInputHeld(true);
}

void UStuckRecoveryInputComponent::Released()
{
	if (bInputAccepted && Recovery.IsValid()) Recovery->SetRecoveryInputHeld(false);
	bInputAccepted = false;
}

void UStuckRecoveryInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	const bool bLocal = Character && Character->IsLocallyControlled() && Controller && Controller->GetPawn() == Character;
	const bool bAvailable = bLocal && Recovery.IsValid() && Recovery->IsEscapeAvailable() && !Controller->IsMoveInputIgnored();
	if (!bAvailable) Released();
	if (!EscapeAction && EscapeKey.IsValid())
	{
		const bool bDown = bLocal && Controller->IsInputKeyDown(EscapeKey);
		if (bDown && !bRawKeyWasDown && bAvailable) Pressed();
		if (!bDown && bRawKeyWasDown) Released();
		bRawKeyWasDown = bDown;
	}
	RefreshGuide(bAvailable && bShowGuide && (EscapeAction || EscapeKey.IsValid()));
}

FText UStuckRecoveryInputComponent::GetKeyLabel() const
{
	if (!EscapeAction) return EscapeKey.GetDisplayName();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Controller && Controller->GetLocalPlayer())
	{
		if (const UEnhancedInputLocalPlayerSubsystem* Subsystem = Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			const TArray<FKey> Keys = Subsystem->QueryKeysMappedToAction(EscapeAction);
			if (Keys.Num() > 0) return Keys[0].GetDisplayName();
		}
	}
	return NSLOCTEXT("StuckRecovery", "Unmapped", "탈출 키 매핑 필요");
}

void UStuckRecoveryInputComponent::RefreshGuide(bool bVisible)
{
	if (!bVisible)
	{
		if (Guide) Guide->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!Guide)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		Guide = CreateWidget<UStuckRecoveryWidget>(Controller, GuideWidgetClass ? GuideWidgetClass.Get() : UStuckRecoveryWidget::StaticClass());
		if (!Guide) return;
		Guide->AddToPlayerScreen(20);
		Guide->SetDesiredSizeInViewport(FVector2D(600.f, 100.f));
		Guide->SetAnchorsInViewport(FAnchors(0.5f, 0.8f));
		Guide->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	}
	Guide->SetRecoverySource(Recovery.Get(), GetKeyLabel());
	Guide->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UStuckRecoveryInputComponent::HandleRecoveryResult(bool bSucceeded)
{
	if (!bSucceeded && Guide) Guide->ShowFailure();
}
