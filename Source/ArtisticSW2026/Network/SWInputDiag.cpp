#include "Network/SWInputDiag.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InputMappingContext.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Network/SWNetworkLog.h"

namespace
{
int32 CurrentAttempt = 0;
int32 LineCount = 0;
bool bHostRole = false;
bool bMoveRecorded = false;
bool bLookRecorded = false;
bool bFileWarningIssued = false;
FString InputDiagPath;
}

void FSWInputDiag::BeginAttempt(UGameInstance* Instance, int32 AttemptId, bool bHost)
{
	CurrentAttempt = AttemptId;
	LineCount = 0;
	bHostRole = bHost;
	bMoveRecorded = false;
	bLookRecorded = false;
	InputDiagPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("SWInputDiag"),
		FString::Printf(TEXT("%u_%d.txt"), FPlatformProcess::GetCurrentProcessId(), AttemptId));
	Record(Instance, TEXT("ConnectionStart"));
}

void FSWInputDiag::Record(UGameInstance* Instance, const TCHAR* Event)
{
	if (!IsInGameThread() || CurrentAttempt <= 0 || LineCount >= 64 || InputDiagPath.IsEmpty()) return;
	APlayerController* Controller = Instance ? Instance->GetFirstLocalPlayerController() : nullptr;
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	bool bMappingPresent = false;
	if (Controller && Controller->GetPawn() && Controller->GetLocalPlayer())
	{
		const FObjectProperty* MappingProperty = FindFProperty<FObjectProperty>(Controller->GetPawn()->GetClass(), TEXT("DefaultIMC"));
		const UInputMappingContext* Context = MappingProperty
			? Cast<UInputMappingContext>(MappingProperty->GetObjectPropertyValue_InContainer(Controller->GetPawn())) : nullptr;
		const UEnhancedInputLocalPlayerSubsystem* Subsystem = Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		bMappingPresent = Context && Subsystem && Subsystem->HasMappingContext(Context);
	}
	const FString Line = FString::Printf(TEXT("%.6f role=%s attempt=%d event=%s viewportIgnore=%d cursor=%d controller=%d pawn=%d mapping=%d\n"),
		FPlatformTime::Seconds(), bHostRole ? TEXT("Host") : TEXT("Guest"), CurrentAttempt, Event,
		Viewport && Viewport->IgnoreInput() ? 1 : 0, Controller && Controller->bShowMouseCursor ? 1 : 0,
		Controller ? 1 : 0, Controller && Controller->GetPawn() ? 1 : 0,
		bMappingPresent ? 1 : 0);
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!Files.CreateDirectoryTree(*FPaths::GetPath(InputDiagPath))
		|| !FFileHelper::SaveStringToFile(Line, *InputDiagPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		if (!bFileWarningIssued)
		{
			UE_LOG(LogSWConnection, Warning, TEXT("SWInputDiag file could not be opened"));
			bFileWarningIssued = true;
		}
		return;
	}
	++LineCount;
}

void FSWInputDiag::RecordFirstMove(UGameInstance* Instance)
{
	if (!bMoveRecorded) { bMoveRecorded = true; Record(Instance, TEXT("FirstMove")); }
}

void FSWInputDiag::RecordFirstLook(UGameInstance* Instance)
{
	if (!bLookRecorded) { bLookRecorded = true; Record(Instance, TEXT("FirstLook")); }
}
