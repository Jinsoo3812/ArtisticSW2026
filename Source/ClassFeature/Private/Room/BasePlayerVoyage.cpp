#include "BasePlayerController.h"
#include "BasePlayer.h"
#include "Ship.h"
#include "Room/ClassFeatureRoomProgressSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWRoomReadyState.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Network/SWConnectionSubsystem.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "UI/SWVoyageFailureWidget.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreaming.h"
#include "MultiGameMode.h"

void ABasePlayerController::BeginVoyageBindings()
{
	if (VoyageTickerHandle.IsValid()) return;
	VoyageTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ABasePlayerController::TickLocalVoyage));
}

void ABasePlayerController::EndVoyageBindings()
{
	if (VoyageTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(VoyageTickerHandle);
	VoyageTickerHandle.Reset();
	if (ASWRoomReadyState* Ready = BoundVoyageReady.Get()) Ready->OnVoyageStateChanged.Remove(VoyageReadyHandle);
	BoundVoyageReady.Reset(); VoyageReadyHandle.Reset(); RemoveVoyageFailureWidget();
	if (HasAuthority()) if (UClassFeatureRoomProgressSubsystem* ProgressOwner = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) ProgressOwner->HandleVoyageParticipantLogout(this);
}

void ABasePlayerController::HandleVoyageReplicatedState(const FSWVoyageReplicatedState& State)
{
	if (!IsLocalController() || State.Generation <= 0) return;
	USWVoyageResetSubsystem* Core = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	ASWRoomReadyState* Ready = BoundVoyageReady.Get();
	if (!Core) return;
	if (Core->GetGeneration() == 0 && Ready && Ready->bWorldReady && (State.Phase == ESWVoyagePhase::Idle || State.Phase == ESWVoyagePhase::Release))
	{
		FString Error;
		if (!HasAuthority() && !Core->AdoptInitialClientGeneration(State, Ready->RoomRunId, Error))
			UE_LOG(LogTemp, Error, TEXT("VoyageInitialGeneration: %s"), *Error);
		return;
	}
	// Healthy bootstrap belongs to map readiness; initial Idle/Release + ready adopts the generation.
	if (State.bBootstrap && (State.Phase == ESWVoyagePhase::Restore
		|| State.Phase == ESWVoyagePhase::ClientReady
		|| State.Phase == ESWVoyagePhase::Release
		|| State.Phase == ESWVoyagePhase::Idle)) return;
	if (State.AttemptId < LocalVoyageState.AttemptId || State.Generation < LocalVoyageState.Generation) return;
	if (State.Phase == ESWVoyagePhase::Idle && State.AttemptId == LocalVoyageState.AttemptId && LocalVoyagePhase < ESWVoyagePhase::Unload)
	{ ClientCancelVoyage_Implementation(State.AttemptId, State.Generation); return; }
	if (State.Phase == ESWVoyagePhase::Idle && State.AttemptId == LocalVoyageState.AttemptId && LocalVoyagePhase >= ESWVoyagePhase::Unload)
	{ FSWVoyageReplicatedState Released = State; Released.Phase = ESWVoyagePhase::Release; ClientSetVoyagePhase_Implementation(Released); return; }
	if (State.Phase != ESWVoyagePhase::Idle) ClientSetVoyagePhase_Implementation(State);
}

void ABasePlayerController::ClientBeginVoyage_Implementation(const FSWVoyageReplicatedState& State)
{
	ClientSetVoyagePhase_Implementation(State);
}

void ABasePlayerController::ClientSetVoyagePhase_Implementation(const FSWVoyageReplicatedState& State)
{
	if (!IsLocalController() || State.AttemptId <= 0 || State.Generation <= 0
		|| State.AttemptId < LocalVoyageState.AttemptId || State.Generation < LocalVoyageState.Generation) return;
	// Healthy bootstrap belongs to map readiness; initial Idle/Release + ready adopts the generation.
	if (State.bBootstrap && (State.Phase == ESWVoyagePhase::Restore
		|| State.Phase == ESWVoyagePhase::ClientReady
		|| State.Phase == ESWVoyagePhase::Release
		|| State.Phase == ESWVoyagePhase::Idle)) return;
	if (State.AttemptId == LocalVoyageState.AttemptId && LocalVoyagePhase == ESWVoyagePhase::Release && bVoyageFinishReceived) return;
	if (State.AttemptId == LocalVoyageState.AttemptId && State.Generation == LocalVoyageState.Generation && State.Phase < LocalVoyageState.Phase) return;
	if (State.AttemptId != LocalVoyageState.AttemptId)
	{
		LocalVoyagePhase = ESWVoyagePhase::Idle; LocalVoyageAcks.Reset();
		bVoyagePlacementReceived = false; VoyagePlacementPawn.Reset(); VoyagePlacementShip.Reset();
		bVoyageFinishReceived = false; bVoyageLocalFailed = false; RemoveVoyageFailureWidget();
	}
	LocalVoyageState = State;
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>())
		if (!Connection->BeginInPlaceVoyagePresentation(State))
		{ bVoyageLocalFailed = true; ServerReportVoyageFailure(State.AttemptId, State.Generation, TEXT("VoyagePresentationUnavailable")); return; }
	if (State.Phase == ESWVoyagePhase::Failed)
	{ if (!bVoyageLocalFailed) ClientVoyageFailure_Implementation(State.AttemptId, State.Generation, false, TEXT("항해 준비에 실패했습니다.")); return; }
	if (State.Phase == ESWVoyagePhase::Presentation && LocalVoyageAcks.Contains(ESWVoyageAck::Presentation)) ServerConfirmVoyageStage(State.AttemptId, State.Generation, ESWVoyageAck::Presentation);
	if (State.Phase >= ESWVoyagePhase::Unload && LocalVoyageAcks.Contains(ESWVoyageAck::Unloaded)) ServerConfirmVoyageStage(State.AttemptId, State.Generation, ESWVoyageAck::Unloaded);
	if (State.Phase >= ESWVoyagePhase::Load && LocalVoyageAcks.Contains(ESWVoyageAck::Loaded)) ServerConfirmVoyageStage(State.AttemptId, State.Generation, ESWVoyageAck::Loaded);
	if (State.Phase == ESWVoyagePhase::ClientReady && LocalVoyageAcks.Contains(ESWVoyageAck::Ready)) ServerConfirmVoyageStage(State.AttemptId, State.Generation, ESWVoyageAck::Ready);
}

void ABasePlayerController::ClientSetVoyagePlacement_Implementation(int64 AttemptId, int32 Generation, APawn* NewPawn, AShip* Ship, FTransform Target)
{
	if (AttemptId != LocalVoyageState.AttemptId || Generation != LocalVoyageState.Generation || Target.ContainsNaN()) return;
	VoyagePlacementPawn = NewPawn; VoyagePlacementShip = Ship; VoyagePlacementTarget = Target; bVoyagePlacementReceived = true;
}

void ABasePlayerController::ClientFinishVoyage_Implementation(int64 AttemptId, int32 Generation, bool bSaveSucceeded, const FString& Message)
{
	if (AttemptId != LocalVoyageState.AttemptId || Generation != LocalVoyageState.Generation) return;
	bVoyageFinishReceived = true; bVoyageFinishSaveSucceeded = bSaveSucceeded; VoyageFinishMessage = Message;
	if (LocalVoyageState.Reason == ESWVoyageReason::GameOverRetry) PendingRetryRequestId = 0;
}

void ABasePlayerController::ClientCancelVoyage_Implementation(int64 AttemptId, int32 Generation)
{
	if (AttemptId != LocalVoyageState.AttemptId || Generation != LocalVoyageState.Generation) return;
	if (LocalVoyageState.Reason == ESWVoyageReason::GameOverRetry)
	{ PendingRetryRequestId = 0; RetryStatus = TEXT("다시 시작 준비가 취소되었습니다. 다시 시도하세요."); }
	USWVoyageResetSubsystem* Core = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	if (!HasAuthority() && Core && LocalVoyagePhase < ESWVoyagePhase::Unload)
	{
		FSWVoyageResetContext Context; Context.AttemptId = AttemptId; Context.Generation = Generation; Context.Phase = ESWVoyagePhase::Idle;
		Context.GameplayPackage = LocalVoyageState.GameplayPackage; FString Error; Core->BeginLocalPhase(Context, Error);
	}
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>()) Connection->CancelInPlaceVoyagePresentation(AttemptId, Generation);
	LocalVoyageState.Phase = ESWVoyagePhase::Idle; LocalVoyagePhase = ESWVoyagePhase::Idle; RemoveVoyageFailureWidget();
}

void ABasePlayerController::RemoveVoyageFailureWidget()
{
	if (VoyageFailureWidget) { VoyageFailureWidget->RemoveFromParent(); VoyageFailureWidget = nullptr; }
}

void ABasePlayerController::ClientVoyageFailure_Implementation(int64 AttemptId, int32 Generation, bool bHost, const FString& Error)
{
	if (AttemptId != LocalVoyageState.AttemptId || Generation != LocalVoyageState.Generation || !IsLocalController()) return;
	bVoyageLocalFailed = true;
	if (!HasAuthority()) if (USWVoyageResetSubsystem* Core = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>())
	{
		FSWVoyageResetContext Context; Context.AttemptId = AttemptId; Context.Generation = Generation;
		Context.GameplayPackage = LocalVoyageState.GameplayPackage; Context.Phase = ESWVoyagePhase::Failed;
		FString FailureError; Core->BeginLocalPhase(Context, FailureError);
	}
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>()) Connection->SetVoyageFailure(AttemptId, Generation, Error.Left(512));
	if (!VoyageFailureWidget) VoyageFailureWidget = CreateWidget<USWVoyageFailureWidget>(this, USWVoyageFailureWidget::StaticClass());
	if (VoyageFailureWidget)
	{
		VoyageFailureWidget->Configure(bHost, Error.Left(512),
			FSimpleDelegate::CreateUObject(this, &ABasePlayerController::RetryLocalVoyageFailure), FSimpleDelegate::CreateUObject(this, &ABasePlayerController::LeaveLocalVoyageFailure));
		VoyageFailureWidget->AddToViewport(1000);
		FInputModeUIOnly Input; Input.SetWidgetToFocus(VoyageFailureWidget->TakeWidget()); SetInputMode(Input); bShowMouseCursor = true;
	}
}

void ABasePlayerController::RetryLocalVoyageFailure()
{
	ServerRetryVoyageFailure(LocalVoyageState.AttemptId, LocalVoyageState.Generation);
}

void ABasePlayerController::LeaveLocalVoyageFailure()
{
	RemoveVoyageFailureWidget();
	if (USWRoomSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomSubsystem>()) Room->LeaveRoom();
}

void ABasePlayerController::ServerConfirmVoyageStage_Implementation(int64 AttemptId, int32 Generation, ESWVoyageAck Ack)
{
	if (UClassFeatureRoomProgressSubsystem* ProgressOwner = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) ProgressOwner->HandleVoyageStageAck(this, AttemptId, Generation, Ack);
}
void ABasePlayerController::ServerReportVoyageFailure_Implementation(int64 AttemptId, int32 Generation, const FString& Error)
{
	if (UClassFeatureRoomProgressSubsystem* ProgressOwner = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) ProgressOwner->ReportVoyageFailure(this, AttemptId, Generation, Error.Left(512));
}
void ABasePlayerController::ServerRetryVoyageFailure_Implementation(int64 AttemptId, int32 Generation)
{
	if (UClassFeatureRoomProgressSubsystem* ProgressOwner = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) ProgressOwner->RetryVoyageFailure(this, AttemptId, Generation);
}

bool ABasePlayerController::TickLocalVoyage(float DeltaSeconds)
{
	if (!GetWorld() || !GetGameInstance()) return true;
	if (HasAuthority()) if (UClassFeatureRoomProgressSubsystem* ProgressOwner = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) ProgressOwner->HandleVoyageParticipantLogin(this);
	if (!IsLocalController()) return true;
	if (!BoundVoyageReady.IsValid())
		for (TActorIterator<ASWRoomReadyState> It(GetWorld()); It; ++It)
		{
			BoundVoyageReady = *It; VoyageReadyHandle = It->OnVoyageStateChanged.AddUObject(this, &ABasePlayerController::HandleVoyageReplicatedState);
			HandleVoyageReplicatedState(It->VoyageState); break;
		}
	if (BoundVoyageReady.IsValid() && LocalVoyageState.AttemptId == 0) HandleVoyageReplicatedState(BoundVoyageReady->VoyageState);
	if (LocalVoyageState.Phase == ESWVoyagePhase::Idle || bVoyageLocalFailed || LocalVoyageState.Phase == ESWVoyagePhase::RecoveryTravel) return true;
	USWVoyageResetSubsystem* Core = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>(); if (!Core) return true;
	FString Error;
	auto Ack = [this](ESWVoyageAck Stage)
	{
		if (!LocalVoyageAcks.Contains(Stage)) { LocalVoyageAcks.Add(Stage); ServerConfirmVoyageStage(LocalVoyageState.AttemptId, LocalVoyageState.Generation, Stage); }
	};
	auto Begin = [this, Core, &Error](ESWVoyagePhase Phase)
	{
		FSWVoyageResetContext Context; Context.AttemptId = LocalVoyageState.AttemptId; Context.Generation = LocalVoyageState.Generation;
		Context.Reason = LocalVoyageState.Reason; Context.Phase = Phase; Context.GameplayPackage = LocalVoyageState.GameplayPackage;
		Context.bBootstrap = LocalVoyageState.bBootstrap; Context.bContinue = LocalVoyageState.bContinue;
		if (!HasAuthority() && !Core->BeginLocalPhase(Context, Error)) return false;
		LocalVoyagePhase = Phase; return true;
	};
	if (LocalVoyagePhase == ESWVoyagePhase::Idle)
	{
		if (!Begin(ESWVoyagePhase::Presentation)) goto Failed;
		Ack(ESWVoyageAck::Presentation);
	}
	if (LocalVoyageState.Phase >= ESWVoyagePhase::Quiesce && LocalVoyagePhase == ESWVoyagePhase::Presentation)
		if (!Begin(ESWVoyagePhase::Quiesce)) goto Failed;
	if (LocalVoyagePhase == ESWVoyagePhase::Quiesce && !HasAuthority())
	{
		const ESWVoyageStepResult Result = Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Failed) goto Failed;
		if (Result == ESWVoyageStepResult::Pending) return true;
	}
	if (LocalVoyageState.Phase >= ESWVoyagePhase::Unload && LocalVoyagePhase == ESWVoyagePhase::Quiesce)
	{
		ClearVoyageLocalPresentation(LocalVoyageState.Generation);
		if (!Begin(LocalVoyageState.bBootstrap ? ESWVoyagePhase::Load : ESWVoyagePhase::Unload)) goto Failed;
	}
	if (LocalVoyagePhase == ESWVoyagePhase::Unload || LocalVoyagePhase == ESWVoyagePhase::Purge)
	{
		const ESWVoyageStepResult Result = HasAuthority() ? ESWVoyageStepResult::Succeeded : Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Failed) goto Failed;
		if (Result == ESWVoyageStepResult::Pending) return true;
		if (LocalVoyagePhase == ESWVoyagePhase::Unload) { if (!Begin(ESWVoyagePhase::Purge)) goto Failed; return true; }
		Ack(ESWVoyageAck::Unloaded);
		if (LocalVoyageState.Phase >= ESWVoyagePhase::Load && !Begin(ESWVoyagePhase::Load)) goto Failed;
	}
	if (LocalVoyagePhase == ESWVoyagePhase::Load)
	{
		const ESWVoyageStepResult Result = HasAuthority() ? ESWVoyageStepResult::Succeeded : Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Failed) goto Failed;
		if (Result == ESWVoyageStepResult::Pending) return true;
		Ack(ESWVoyageAck::Loaded);
		if (LocalVoyageState.Phase >= ESWVoyagePhase::Restore && !Begin(ESWVoyagePhase::Restore)) goto Failed;
	}
	if (LocalVoyagePhase == ESWVoyagePhase::Restore)
	{
		const ESWVoyageStepResult Result = HasAuthority() ? ESWVoyageStepResult::Succeeded : Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Failed) goto Failed;
		if (Result == ESWVoyageStepResult::Pending) return true;
		if (LocalVoyageState.Phase >= ESWVoyagePhase::ClientReady && !Begin(ESWVoyagePhase::ClientReady)) goto Failed;
	}
	if (LocalVoyagePhase == ESWVoyagePhase::ClientReady)
	{
		const ESWVoyageStepResult Result = HasAuthority() ? ESWVoyageStepResult::Succeeded : Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Failed) goto Failed;
		if (Result == ESWVoyageStepResult::Pending) return true;
		APawn* NewPawn = VoyagePlacementPawn.Get(); AShip* Ship = VoyagePlacementShip.Get();
		if (!bVoyagePlacementReceived || !NewPawn || !Ship || GetPawn() != NewPawn || NewPawn->GetController() != this
			|| Core->GetActorGeneration(NewPawn) != LocalVoyageState.Generation || Core->GetActorGeneration(Ship) != LocalVoyageState.Generation) return true;
		ABasePlayer* LifePlayer = Cast<ABasePlayer>(NewPawn);
		if (!LifePlayer) { Error = TEXT("VoyageClientPlacementPawnContractInvalid"); goto Failed; }
		if (!LifePlayer->IsVoyageClientLifeReady(LocalVoyageState.Generation) || !Ship->IsVoyagePhysicsReady(LocalVoyageState.Generation)) return true;
		if (FVector::Dist(NewPawn->GetActorLocation(), VoyagePlacementTarget.GetLocation()) > 100.0
			|| FMath::Abs(FMath::FindDeltaAngleDegrees(NewPawn->GetActorRotation().Yaw, VoyagePlacementTarget.Rotator().Yaw)) > 5.0) return true;
		Ack(ESWVoyageAck::Ready);
		ASWRoomReadyState* Ready = BoundVoyageReady.Get();
		if (bVoyageFinishReceived && Ready && Ready->bWorldReady && Ready->RestoreGeneration == LocalVoyageState.Generation
			&& Ready->VoyageState.AttemptId == LocalVoyageState.AttemptId && Ready->VoyageState.Generation == LocalVoyageState.Generation
			&& (Ready->VoyageState.Phase == ESWVoyagePhase::Release || Ready->VoyageState.Phase == ESWVoyagePhase::Idle))
		{
			if (!Begin(ESWVoyagePhase::Release)) goto Failed;
			if (!HasAuthority())
			{
				USWRoomSnapshotSubsystem* Snapshot = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
				if (Snapshot && Snapshot->IsDiscardingVoyage() && !Snapshot->EndVoyageDiscard(LocalVoyageState.Generation, Error)) goto Failed;
			}
			if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>())
				Connection->CompleteInPlaceVoyagePresentation(LocalVoyageState.AttemptId, LocalVoyageState.Generation, bVoyageFinishSaveSucceeded, VoyageFinishMessage);
			RemoveVoyageFailureWidget(); LocalVoyageState.Phase = ESWVoyagePhase::Idle;
			if (!HasAuthority())
			{
				FSWVoyageResetContext Completed; Completed.AttemptId = LocalVoyageState.AttemptId; Completed.Generation = LocalVoyageState.Generation;
				Completed.GameplayPackage = LocalVoyageState.GameplayPackage; Completed.Phase = ESWVoyagePhase::Idle;
				if (!Core->BeginLocalPhase(Completed, Error)) goto Failed;
			}
		}
	}
	return true;
Failed:
	bVoyageLocalFailed = true; ServerReportVoyageFailure(LocalVoyageState.AttemptId, LocalVoyageState.Generation, Error.Left(512)); return true;
}
