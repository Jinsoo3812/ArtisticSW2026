#include "Camera/SWPlayerCameraZoomComponent.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"

USWPlayerCameraZoomComponent::USWPlayerCameraZoomComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float USWPlayerCameraZoomComponent::ClampDistance(float Distance) const
{
	const float A = FMath::IsFinite(MinDistance) ? FMath::Max(10.f, MinDistance) : 150.f;
	const float B = FMath::IsFinite(MaxDistance) ? FMath::Max(10.f, MaxDistance) : 800.f;
	return FMath::Clamp(FMath::IsFinite(Distance) ? Distance : 400.f, FMath::Min(A, B), FMath::Max(A, B));
}

void USWPlayerCameraZoomComponent::InitializeDistance(float DefaultDistance)
{
	if (bInitialized) return;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ABasePlayerController* PC = Pawn ? Cast<ABasePlayerController>(Pawn->GetController()) : nullptr;
	PreferredDistance = ClampDistance(PC && PC->IsLocalController() && PC->LocalPreferredCameraDistance > 0.f
		? PC->LocalPreferredCameraDistance : DefaultDistance);
	bInitialized = true;
	// A default is not an explicit local preference. Do not let it mask a saved
	// room distance restored later in the initial possession/BeginPlay lifecycle.
}

void USWPlayerCameraZoomComponent::SetPreferredDistance(float Distance)
{
	if (!FMath::IsFinite(Distance)) return;
	PreferredDistance = ClampDistance(Distance);
	bInitialized = true;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && Pawn->IsLocallyControlled())
	{
		if (ABasePlayerController* PC = Cast<ABasePlayerController>(Pawn->GetController()))
		{
			PC->LocalPreferredCameraDistance = PreferredDistance;
		}
	}
}

void USWPlayerCameraZoomComponent::RestoreDistance(float SavedDistance)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ABasePlayerController* PC = Pawn ? Cast<ABasePlayerController>(Pawn->GetController()) : nullptr;
	SetPreferredDistance(PC && PC->IsLocalController() && PC->LocalPreferredCameraDistance > 0.f
		? PC->LocalPreferredCameraDistance : SavedDistance);
}

float USWPlayerCameraZoomComponent::GetDistance() const { return ClampDistance(PreferredDistance); }
float USWPlayerCameraZoomComponent::GetSafeInterpSpeed() const
{
	return FMath::IsFinite(InterpSpeed) ? FMath::Max(.1f, InterpSpeed) : 10.f;
}
float USWPlayerCameraZoomComponent::GetSprintDistance(float DefaultDistance, float SprintDistance) const
{
	const float Offset = FMath::IsFinite(SprintDistance - DefaultDistance) ? FMath::Max(0.f, SprintDistance - DefaultDistance) : 0.f;
	return ClampDistance(GetDistance() + Offset);
}

void USWPlayerCameraZoomComponent::ReserveWheel(UObject* Consumer, bool bReserve)
{
	if (!Consumer) return;
	if (bReserve) WheelConsumers.Add(Consumer);
	else WheelConsumers.Remove(Consumer);
}

bool USWPlayerCameraZoomComponent::ApplyWheelInput(float Axis)
{
	for (auto It = WheelConsumers.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent();
	}
	const ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player || !Player->CanAdjustManualCameraZoom() || !WheelConsumers.IsEmpty()
		|| !FMath::IsFinite(Axis) || FMath::IsNearlyZero(Axis)) return false;
	const float Step = FMath::IsFinite(WheelStep) ? FMath::Clamp(WheelStep, 1.f, 500.f) : 50.f;
	SetPreferredDistance(GetDistance() - FMath::Clamp(Axis, -10.f, 10.f) * Step);
	return true;
}

void USWPlayerCameraZoomComponent::HandleWheel(const FInputActionValue& Value)
{
	ApplyWheelInput(Value.Get<float>());
}

void USWPlayerCameraZoomComponent::BindInput(UEnhancedInputComponent* Input)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	ABasePlayerController* PC = Player ? Cast<ABasePlayerController>(Player->GetController()) : nullptr;
	if (!Input || !PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
	auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Subsystem) return;
	if (BoundInput.IsValid()) BoundInput->RemoveBindingByHandle(BindingHandle);
	if (BoundSubsystem.IsValid() && ZoomContext) BoundSubsystem->RemoveMappingContext(ZoomContext);
	if (!ZoomAction)
	{
		ZoomAction = NewObject<UInputAction>(this);
		ZoomAction->ValueType = EInputActionValueType::Axis1D;
		// Existing selection actions still receive the wheel; reservation prevents zoom.
		ZoomAction->bConsumeInput = false;
		ZoomContext = NewObject<UInputMappingContext>(this);
		ZoomContext->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	}
	BindingHandle = Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ThisClass::HandleWheel).GetHandle();
	BoundInput = Input;
	BoundSubsystem = Subsystem;
	// Normal gameplay/vehicle contexts retain precedence over this fallback context.
	Subsystem->AddMappingContext(ZoomContext, -10);
	if (PC->LocalPreferredCameraDistance > 0.f) SetPreferredDistance(PC->LocalPreferredCameraDistance);
}

void USWPlayerCameraZoomComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundSubsystem.IsValid() && ZoomContext) BoundSubsystem->RemoveMappingContext(ZoomContext);
	if (BoundInput.IsValid()) BoundInput->RemoveBindingByHandle(BindingHandle);
	WheelConsumers.Empty();
	Super::EndPlay(Reason);
}
