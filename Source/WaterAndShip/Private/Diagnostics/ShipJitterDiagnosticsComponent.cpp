#include "Diagnostics/ShipJitterDiagnosticsComponent.h"

#include "Ship.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Containers/Queue.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* GetDiagnosticNetModeName(const UWorld* World)
	{
		if (!World) return TEXT("NoWorld");
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	float RotationDeltaDegrees(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(A.AngularDistance(B));
	}
}

struct FShipRenderProbeSample
{
	double WorldTime = 0.0;
	float DeltaMs = 0.0f;
	uint64 RequestGameFrame = 0;
	uint64 RenderFrame = 0;
	FTransform RequestedRootTransform = FTransform::Identity;
	FTransform RequestedVisualTransform = FTransform::Identity;
	FTransform RequestedCameraTransform = FTransform::Identity;
	FVector2D RequestedScreenPosition = FVector2D::ZeroVector;
	float RequestedScreenDeltaPx = 0.0f;
	bool bCameraValid = false;
	bool bScreenValid = false;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
	FTransform RenderProxyTransform = FTransform::Identity;
};

struct FShipRenderProbeState
{
	TQueue<FShipRenderProbeSample, EQueueMode::Mpsc> CompletedSamples;
};

class FShipRenderProbeViewExtension final : public FWorldSceneViewExtension
{
public:
	FShipRenderProbeViewExtension(
		const FAutoRegister& AutoRegister,
		UWorld* World,
		AShip* Ship,
		const TSharedPtr<FShipRenderProbeState, ESPMode::ThreadSafe>& ProbeState,
		const FVector& InMarkerLocalPosition)
		: FWorldSceneViewExtension(AutoRegister, World)
		, CachedShip(Ship)
		, State(ProbeState)
		, MarkerLocalPosition(InMarkerLocalPosition)
	{
	}

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
	{
		AShip* Ship = CachedShip.Get();
		const TSharedPtr<FShipRenderProbeState, ESPMode::ThreadSafe> ProbeState = State.Pin();
		if (!Ship || !Ship->BuoyancyRoot || !Ship->ShipVisualMesh || !ProbeState || InViewFamily.Views.IsEmpty())
		{
			return;
		}

		FPrimitiveSceneProxy* SceneProxy = Ship->ShipVisualMesh->GetSceneProxy();
		const FSceneView* View = InViewFamily.Views[0];
		if (!SceneProxy || !View)
		{
			return;
		}

		FShipRenderProbeSample RenderSample;
		RenderSample.WorldTime = Ship->GetWorld() ? Ship->GetWorld()->GetTimeSeconds() : 0.0;
		RenderSample.DeltaMs = Ship->GetWorld() ? Ship->GetWorld()->GetDeltaSeconds() * 1000.0f : 0.0f;
		RenderSample.RequestGameFrame = InViewFamily.FrameCounter;
		RenderSample.SceneProxy = SceneProxy;
		RenderSample.RequestedRootTransform = Ship->BuoyancyRoot->GetComponentTransform();
		RenderSample.RequestedVisualTransform = Ship->ShipVisualMesh->GetComponentTransform();
		RenderSample.RequestedCameraTransform = FTransform(View->ViewMatrices.GetInvViewMatrix());
		RenderSample.bCameraValid = true;
		const FVector MarkerWorldPosition = RenderSample.RequestedVisualTransform.TransformPosition(MarkerLocalPosition);
		RenderSample.bScreenValid = View->WorldToPixel(MarkerWorldPosition, RenderSample.RequestedScreenPosition);
		RenderSample.RequestedScreenDeltaPx = bHasPreviousScreenPosition && RenderSample.bScreenValid
			? FVector2D::Distance(RenderSample.RequestedScreenPosition, PreviousScreenPosition) : 0.0f;

		if (RenderSample.bScreenValid)
		{
			PreviousScreenPosition = RenderSample.RequestedScreenPosition;
			bHasPreviousScreenPosition = true;
		}

		PendingSamples.Enqueue(MoveTemp(RenderSample));
	}

	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override
	{
		const TSharedPtr<FShipRenderProbeState, ESPMode::ThreadSafe> ProbeState = State.Pin();
		if (!ProbeState)
		{
			return;
		}

		FShipRenderProbeSample RenderSample;
		if (PendingSamples.Dequeue(RenderSample) && RenderSample.SceneProxy)
		{
			RenderSample.RenderFrame = static_cast<uint64>(GFrameCounterRenderThread);
			RenderSample.RenderProxyTransform = FTransform(RenderSample.SceneProxy->GetLocalToWorld());
			RenderSample.SceneProxy = nullptr;
			ProbeState->CompletedSamples.Enqueue(MoveTemp(RenderSample));
		}
	}

private:
	TWeakObjectPtr<AShip> CachedShip;
	TWeakPtr<FShipRenderProbeState, ESPMode::ThreadSafe> State;
	TQueue<FShipRenderProbeSample, EQueueMode::Spsc> PendingSamples;
	FVector MarkerLocalPosition = FVector::ZeroVector;
	FVector2D PreviousScreenPosition = FVector2D::ZeroVector;
	bool bHasPreviousScreenPosition = false;
};

UShipJitterDiagnosticsComponent::UShipJitterDiagnosticsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UShipJitterDiagnosticsComponent::BeginPlay()
{
	Super::BeginPlay();

	AShip* Ship = Cast<AShip>(GetOwner());
	if (!Ship || !FParse::Param(FCommandLine::Get(), TEXT("ShipJitterDiagnostics")) || Ship->IsEnemyShipForEffects())
	{
		SetComponentTickEnabled(false);
		return;
	}

	CachedShip = Ship;
	RenderProbeState = MakeShared<FShipRenderProbeState, ESPMode::ThreadSafe>();
	bAutoCamera = FParse::Param(FCommandLine::Get(), TEXT("ShipJitterAutoCamera"));
	if (Ship->ShipVisualMesh)
	{
		MarkerLocalPosition = Ship->ShipVisualMesh->GetComponentTransform().InverseTransformPosition(
			Ship->ShipVisualMesh->Bounds.Origin);
	}
	if (!IsRunningDedicatedServer())
	{
		RenderViewExtension = FSceneViewExtensions::NewExtension<FShipRenderProbeViewExtension>(
			GetWorld(), Ship, RenderProbeState, MarkerLocalPosition);
	}

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShipDiagnostics"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString FileName = FString::Printf(
		TEXT("ShipJitter_%s_%s_%s_%u.csv"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),
		GetDiagnosticNetModeName(GetWorld()),
		*FPaths::MakeValidFileName(Ship->GetName()),
		FPlatformProcess::GetCurrentProcessId());
	const FString FullPath = FPaths::Combine(Directory, FileName);
	const FString RenderFullPath = FPaths::Combine(
		Directory,
		FPaths::GetBaseFilename(FileName) + TEXT("_Render.csv"));
	CsvArchive.Reset(IFileManager::Get().CreateFileWriter(*FullPath));
	if (!CsvArchive)
	{
		UE_LOG(LogTemp, Error, TEXT("[SHIP-JITTER] Failed to create %s"), *FullPath);
		return;
	}
	RenderCsvArchive.Reset(IFileManager::Get().CreateFileWriter(*RenderFullPath));
	if (!RenderCsvArchive)
	{
		UE_LOG(LogTemp, Error, TEXT("[SHIP-RENDER] Failed to create %s"), *RenderFullPath);
		CloseCsv();
		return;
	}

	WriteCsvLine(TEXT("Time,GameFrame,DeltaMs,NetMode,Role,Ship,RootX,RootY,RootZ,RootPitch,RootYaw,RootRoll,VisualX,VisualY,VisualZ,CameraValid,CameraX,CameraY,CameraZ,CameraPitch,CameraYaw,CameraRoll,ControlPitch,ControlYaw,ControlRoll,PlayerX,PlayerY,PlayerZ,BaseName,BaseX,BaseY,BaseZ,RelX,RelY,RelZ,RelPitch,RelYaw,RelRoll,ScreenValid,ScreenX,ScreenY,RootDeltaCm,RootDeltaDeg,CameraDeltaCm,CameraDeltaDeg,RelDeltaCm,RelDeltaDeg,ScreenDeltaPx,PTValid,PTStep,ServerFrame,TickOffset,Resim,ResimStepCount,LastResimStep,PTX,PTY,PTZ,RootPTDeltaCm,CorrectionSerial,CorrectionFrame,CorrectionCm,CorrectionDeg,RippleRevision,RippleHash,RippleActive"));
	WriteRenderCsvLine(TEXT("ConsumeTime,ConsumeGameFrame,RequestTime,RequestGameFrame,RenderFrame,QueueLagGameFrames,RenderFrameDelta,RequestVisualDeltaCm,RequestVisualDeltaDeg,RenderProxyDeltaCm,RenderProxyDeltaDeg,SubmitDeltaCm,SubmitDeltaDeg,DeltaMs,RootX,RootY,RootZ,RequestVisualX,RequestVisualY,RequestVisualZ,RenderVisualX,RenderVisualY,RenderVisualZ,CameraValid,CameraX,CameraY,CameraZ,ScreenValid,ScreenX,ScreenY,ScreenDeltaPx"));
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Warning, TEXT("[SHIP-JITTER] Capture started: %s RenderCapture=%s AutoCamera=%d"), *FullPath, *RenderFullPath, bAutoCamera ? 1 : 0);
}

void UShipJitterDiagnosticsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseCsv();
	Super::EndPlay(EndPlayReason);
}

void UShipJitterDiagnosticsComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AShip* Ship = CachedShip.Get();
	UWorld* World = GetWorld();
	if (!Ship || !World || !CsvArchive || !Ship->BuoyancyRoot || !Ship->ShipVisualMesh)
	{
		return;
	}

	const FTransform RootTransform = Ship->BuoyancyRoot->GetComponentTransform();
	const FTransform VisualTransform = Ship->ShipVisualMesh->GetComponentTransform();
	const FVector MarkerWorldPosition = VisualTransform.TransformPosition(MarkerLocalPosition);

	APlayerController* PlayerController = World->GetFirstPlayerController();
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	ACharacter* Character = Cast<ACharacter>(PlayerPawn);
	UPrimitiveComponent* MovementBase = Character && Character->GetCharacterMovement()
		? Character->GetCharacterMovement()->GetMovementBase()
		: nullptr;
	const FTransform BaseTransform = MovementBase ? MovementBase->GetComponentTransform() : FTransform::Identity;

	bool bCameraValid = false;
	FTransform CameraTransform = FTransform::Identity;
	FRotator ControlRotation = FRotator::ZeroRotator;
	if (PlayerController)
	{
		ControlRotation = PlayerController->GetControlRotation();
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			const FMinimalViewInfo& POV = CameraManager->GetCameraCacheView();
			CameraTransform = FTransform(POV.Rotation, POV.Location);
			bCameraValid = true;
		}
	}

	const FTransform RelativeTransform = bCameraValid
		? VisualTransform.GetRelativeTransform(CameraTransform)
		: FTransform::Identity;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	const bool bScreenValid = PlayerController
		&& PlayerController->ProjectWorldLocationToScreen(MarkerWorldPosition, ScreenPosition, true);

	if (bAutoCamera && PlayerController && Character && MovementBase && MovementBase->GetOwner() == Ship && bCameraValid)
	{
		if (!bHasAutoCameraBaseRotation)
		{
			AutoCameraBaseRotation = ControlRotation;
			AutoCameraBaseRotation.Pitch = -15.0f;
			AutoCameraBaseRotation.Roll = 0.0f;
			bHasAutoCameraBaseRotation = true;
		}
		FRotator TargetRotation = AutoCameraBaseRotation;
		TargetRotation.Yaw += FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 0.8f) * 0.8f;
		PlayerController->SetControlRotation(TargetRotation);
	}

	const float RootDeltaCm = bHasPreviousSample
		? FVector::Distance(RootTransform.GetLocation(), PreviousRootTransform.GetLocation()) : 0.0f;
	const float RootDeltaDeg = bHasPreviousSample
		? RotationDeltaDegrees(RootTransform.GetRotation(), PreviousRootTransform.GetRotation()) : 0.0f;
	const float CameraDeltaCm = bHasPreviousSample && bCameraValid
		? FVector::Distance(CameraTransform.GetLocation(), PreviousCameraTransform.GetLocation()) : 0.0f;
	const float CameraDeltaDeg = bHasPreviousSample && bCameraValid
		? RotationDeltaDegrees(CameraTransform.GetRotation(), PreviousCameraTransform.GetRotation()) : 0.0f;
	const float RelativeDeltaCm = bHasPreviousSample && bCameraValid
		? FVector::Distance(RelativeTransform.GetLocation(), PreviousRelativeTransform.GetLocation()) : 0.0f;
	const float RelativeDeltaDeg = bHasPreviousSample && bCameraValid
		? RotationDeltaDegrees(RelativeTransform.GetRotation(), PreviousRelativeTransform.GetRotation()) : 0.0f;
	const float ScreenDeltaPx = bHasPreviousSample && bScreenValid
		? FVector2D::Distance(ScreenPosition, PreviousScreenPosition) : 0.0f;

	if (RenderProbeState)
	{
		FShipRenderProbeSample RenderSample;
		while (RenderProbeState->CompletedSamples.Dequeue(RenderSample))
		{
			const float SubmitDeltaCm = FVector::Distance(
				RenderSample.RequestedVisualTransform.GetLocation(),
				RenderSample.RenderProxyTransform.GetLocation());
			const float SubmitDeltaDeg = RotationDeltaDegrees(
				RenderSample.RequestedVisualTransform.GetRotation(),
				RenderSample.RenderProxyTransform.GetRotation());
			const float RequestedVisualDeltaCm = bHasPreviousRenderSample
				? FVector::Distance(
					RenderSample.RequestedVisualTransform.GetLocation(),
					PreviousRequestedVisualTransform.GetLocation()) : 0.0f;
			const float RequestedVisualDeltaDeg = bHasPreviousRenderSample
				? RotationDeltaDegrees(
					RenderSample.RequestedVisualTransform.GetRotation(),
					PreviousRequestedVisualTransform.GetRotation()) : 0.0f;
			const float RenderProxyDeltaCm = bHasPreviousRenderSample
				? FVector::Distance(
					RenderSample.RenderProxyTransform.GetLocation(),
					PreviousRenderProxyTransform.GetLocation()) : 0.0f;
			const float RenderProxyDeltaDeg = bHasPreviousRenderSample
				? RotationDeltaDegrees(
					RenderSample.RenderProxyTransform.GetRotation(),
					PreviousRenderProxyTransform.GetRotation()) : 0.0f;
			const uint64 QueueLagGameFrames = static_cast<uint64>(GFrameCounter) >= RenderSample.RequestGameFrame
				? static_cast<uint64>(GFrameCounter) - RenderSample.RequestGameFrame : 0;
			const uint64 RenderFrameDelta = bHasPreviousRenderSample && RenderSample.RenderFrame >= PreviousRenderFrame
				? RenderSample.RenderFrame - PreviousRenderFrame : 0;
			const FVector RootPosition = RenderSample.RequestedRootTransform.GetLocation();
			const FVector RequestVisualPosition = RenderSample.RequestedVisualTransform.GetLocation();
			const FVector RenderVisualPosition = RenderSample.RenderProxyTransform.GetLocation();
			const FVector RequestCameraPosition = RenderSample.RequestedCameraTransform.GetLocation();

			WriteRenderCsvLine(FString::Printf(
				TEXT("%.6f,%llu,%.6f,%llu,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f"),
				World->GetTimeSeconds(), static_cast<uint64>(GFrameCounter),
				RenderSample.WorldTime, RenderSample.RequestGameFrame, RenderSample.RenderFrame,
				QueueLagGameFrames, RenderFrameDelta,
				RequestedVisualDeltaCm, RequestedVisualDeltaDeg,
				RenderProxyDeltaCm, RenderProxyDeltaDeg,
				SubmitDeltaCm, SubmitDeltaDeg, RenderSample.DeltaMs,
				RootPosition.X, RootPosition.Y, RootPosition.Z,
				RequestVisualPosition.X, RequestVisualPosition.Y, RequestVisualPosition.Z,
				RenderVisualPosition.X, RenderVisualPosition.Y, RenderVisualPosition.Z,
				RenderSample.bCameraValid ? 1 : 0,
				RequestCameraPosition.X, RequestCameraPosition.Y, RequestCameraPosition.Z,
				RenderSample.bScreenValid ? 1 : 0,
				RenderSample.RequestedScreenPosition.X, RenderSample.RequestedScreenPosition.Y,
				RenderSample.RequestedScreenDeltaPx));

			const bool bProxySubmissionMismatch = SubmitDeltaCm > 0.05f || SubmitDeltaDeg > 0.01f;
			const bool bProxyHeldWhileGameTransformMoved = bHasPreviousRenderSample
				&& (RequestedVisualDeltaCm > 0.10f || RequestedVisualDeltaDeg > 0.01f)
				&& RenderProxyDeltaCm < 0.01f
				&& RenderProxyDeltaDeg < 0.001f;
			if (bProxySubmissionMismatch || bProxyHeldWhileGameTransformMoved || QueueLagGameFrames > 2)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[SHIP-RENDER-ANOMALY] ReqGF=%llu RTFrame=%llu QueueLag=%llu Submit=%.4fcm/%.5fdeg RequestStep=%.4fcm/%.5fdeg ProxyStep=%.4fcm/%.5fdeg ScreenStep=%.3fpx Mismatch=%d Hold=%d"),
					RenderSample.RequestGameFrame, RenderSample.RenderFrame, QueueLagGameFrames,
					SubmitDeltaCm, SubmitDeltaDeg,
					RequestedVisualDeltaCm, RequestedVisualDeltaDeg,
					RenderProxyDeltaCm, RenderProxyDeltaDeg,
					RenderSample.RequestedScreenDeltaPx,
					bProxySubmissionMismatch ? 1 : 0,
					bProxyHeldWhileGameTransformMoved ? 1 : 0);
			}

			PreviousRenderRequestFrame = RenderSample.RequestGameFrame;
			PreviousRenderFrame = RenderSample.RenderFrame;
			PreviousRequestedVisualTransform = RenderSample.RequestedVisualTransform;
			PreviousRenderProxyTransform = RenderSample.RenderProxyTransform;
			bHasPreviousRenderSample = true;
		}
	}

	const FShipRuntimeDiagnosticSnapshot& PT = Ship->GetRuntimeDiagnosticSnapshot();
	const float RootPTDeltaCm = PT.bValid
		? FVector::Distance(RootTransform.GetLocation(), PT.PhysicsPosition) : 0.0f;
	const FRotator RootRotation = RootTransform.Rotator();
	const FRotator CameraRotation = CameraTransform.Rotator();
	const FRotator RelativeRotation = RelativeTransform.Rotator();
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	WriteCsvLine(FString::Printf(
		TEXT("%.6f,%llu,%.4f,%s,%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%llu,%d,%.6f,%.6f,%.6f,%.6f,%llu,%d,%.6f,%.6f,%u,%llu,%d"),
		World->GetTimeSeconds(),
		static_cast<uint64>(GFrameCounter),
		DeltaTime * 1000.0f,
		GetDiagnosticNetModeName(World),
		static_cast<int32>(Ship->GetLocalRole()),
		*Ship->GetName(),
		RootTransform.GetLocation().X, RootTransform.GetLocation().Y, RootTransform.GetLocation().Z,
		RootRotation.Pitch, RootRotation.Yaw, RootRotation.Roll,
		VisualTransform.GetLocation().X, VisualTransform.GetLocation().Y, VisualTransform.GetLocation().Z,
		bCameraValid ? 1 : 0,
		CameraTransform.GetLocation().X, CameraTransform.GetLocation().Y, CameraTransform.GetLocation().Z,
		CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
		ControlRotation.Pitch, ControlRotation.Yaw, ControlRotation.Roll,
		PlayerLocation.X, PlayerLocation.Y, PlayerLocation.Z,
		*GetNameSafe(MovementBase),
		BaseTransform.GetLocation().X, BaseTransform.GetLocation().Y, BaseTransform.GetLocation().Z,
		RelativeTransform.GetLocation().X, RelativeTransform.GetLocation().Y, RelativeTransform.GetLocation().Z,
		RelativeRotation.Pitch, RelativeRotation.Yaw, RelativeRotation.Roll,
		bScreenValid ? 1 : 0, ScreenPosition.X, ScreenPosition.Y,
		RootDeltaCm, RootDeltaDeg, CameraDeltaCm, CameraDeltaDeg,
		RelativeDeltaCm, RelativeDeltaDeg, ScreenDeltaPx,
		PT.bValid ? 1 : 0, PT.PhysicsStep, PT.ServerFrame, PT.NetworkPhysicsTickOffset,
		PT.bWasResimming ? 1 : 0,
		PT.ResimStepCount, PT.LastResimPhysicsStep,
		PT.PhysicsPosition.X, PT.PhysicsPosition.Y, PT.PhysicsPosition.Z,
		RootPTDeltaCm,
		PT.CorrectionSerial, PT.LastCorrectionServerFrame,
		PT.LastCorrectionDistanceCm, PT.LastCorrectionRotationDeg,
		PT.RippleRevision, PT.ActiveRippleHash, PT.ActiveRippleCount));

	if (PT.CorrectionSerial != PreviousCorrectionSerial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SHIP-JITTER-CORRECTION] Ship=%s Serial=%llu SF=%d Dist=%.3fcm Rot=%.4fdeg RelDelta=%.3fcm ScreenDelta=%.3fpx RippleRev=%u Active=%d"),
			*Ship->GetName(), PT.CorrectionSerial, PT.LastCorrectionServerFrame,
			PT.LastCorrectionDistanceCm, PT.LastCorrectionRotationDeg,
			RelativeDeltaCm, ScreenDeltaPx, PT.RippleRevision, PT.ActiveRippleCount);
		PreviousCorrectionSerial = PT.CorrectionSerial;
	}

	if (World->GetTimeSeconds() >= NextSummaryTime)
	{
		NextSummaryTime = World->GetTimeSeconds() + 5.0;
		// Keep long-running client captures useful even when PIE/the client is
		// terminated externally after a timed diagnostic run.
		CsvArchive->Flush();
		RenderCsvArchive->Flush();
		UE_LOG(LogTemp, Warning,
			TEXT("[SHIP-JITTER-SUMMARY] Ship=%s Frame=%llu DT=%.2fms RootDelta=%.3fcm CameraDelta=%.3fcm RelDelta=%.3fcm ScreenDelta=%.3fpx PTStep=%d SF=%d Offset=%d Corr=%llu Resim=%d RippleRev=%u Active=%d"),
			*Ship->GetName(), static_cast<uint64>(GFrameCounter), DeltaTime * 1000.0f,
			RootDeltaCm, CameraDeltaCm, RelativeDeltaCm, ScreenDeltaPx,
			PT.PhysicsStep, PT.ServerFrame, PT.NetworkPhysicsTickOffset,
			PT.CorrectionSerial, PT.bWasResimming ? 1 : 0, PT.RippleRevision, PT.ActiveRippleCount);
	}

	PreviousRootTransform = RootTransform;
	PreviousCameraTransform = CameraTransform;
	PreviousRelativeTransform = RelativeTransform;
	PreviousScreenPosition = ScreenPosition;
	bHasPreviousSample = true;
}

void UShipJitterDiagnosticsComponent::WriteCsvLine(const FString& Line)
{
	if (!CsvArchive)
	{
		return;
	}
	const FString WithNewline = Line + LINE_TERMINATOR;
	FTCHARToUTF8 Utf8(*WithNewline);
	CsvArchive->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

void UShipJitterDiagnosticsComponent::WriteRenderCsvLine(const FString& Line)
{
	if (!RenderCsvArchive)
	{
		return;
	}
	const FString WithNewline = Line + LINE_TERMINATOR;
	FTCHARToUTF8 Utf8(*WithNewline);
	RenderCsvArchive->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

void UShipJitterDiagnosticsComponent::CloseCsv()
{
	if (CsvArchive)
	{
		CsvArchive->Flush();
		CsvArchive->Close();
		CsvArchive.Reset();
	}
	if (RenderCsvArchive)
	{
		RenderCsvArchive->Flush();
		RenderCsvArchive->Close();
		RenderCsvArchive.Reset();
	}
	RenderViewExtension.Reset();
	RenderProbeState.Reset();
}
