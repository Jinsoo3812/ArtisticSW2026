#include "Diagnostics/ShipJitterDiagnosticsComponent.h"

#include "Ship.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
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
	bAutoCamera = FParse::Param(FCommandLine::Get(), TEXT("ShipJitterAutoCamera"));
	if (Ship->ShipVisualMesh)
	{
		MarkerLocalPosition = Ship->ShipVisualMesh->GetComponentTransform().InverseTransformPosition(
			Ship->ShipVisualMesh->Bounds.Origin);
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
	CsvArchive.Reset(IFileManager::Get().CreateFileWriter(*FullPath));
	if (!CsvArchive)
	{
		UE_LOG(LogTemp, Error, TEXT("[SHIP-JITTER] Failed to create %s"), *FullPath);
		return;
	}

	WriteCsvLine(TEXT("Time,GameFrame,DeltaMs,NetMode,Role,Ship,RootX,RootY,RootZ,RootPitch,RootYaw,RootRoll,VisualX,VisualY,VisualZ,CameraValid,CameraX,CameraY,CameraZ,CameraPitch,CameraYaw,CameraRoll,ControlPitch,ControlYaw,ControlRoll,PlayerX,PlayerY,PlayerZ,BaseName,BaseX,BaseY,BaseZ,RelX,RelY,RelZ,RelPitch,RelYaw,RelRoll,ScreenValid,ScreenX,ScreenY,RootDeltaCm,RootDeltaDeg,CameraDeltaCm,CameraDeltaDeg,RelDeltaCm,RelDeltaDeg,ScreenDeltaPx,PTValid,PTStep,ServerFrame,TickOffset,Resim,ResimStepCount,LastResimStep,PTX,PTY,PTZ,RootPTDeltaCm,CorrectionSerial,CorrectionFrame,CorrectionCm,CorrectionDeg,RippleRevision,RippleHash,RippleActive"));
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Warning, TEXT("[SHIP-JITTER] Capture started: %s AutoCamera=%d"), *FullPath, bAutoCamera ? 1 : 0);
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

void UShipJitterDiagnosticsComponent::CloseCsv()
{
	if (CsvArchive)
	{
		CsvArchive->Flush();
		CsvArchive->Close();
		CsvArchive.Reset();
	}
}
