#include "Profiling/SWInsightsCaptureSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "LandscapeProxy.h"
#include "LandscapeNaniteComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWInsights, Log, All);

namespace SWInsights
{
	static TWeakObjectPtr<USWInsightsCaptureSubsystem> CaptureOwner;
	static constexpr TCHAR Channels[] = TEXT("cpu,gpu,frame,bookmark,counters,log,rhicommands");

	static TSharedRef<FJsonObject> Camera(UWorld* World)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Location;
			FRotator Rotation;
			PC->GetPlayerViewPoint(Location, Rotation);
			Result->SetStringField(TEXT("location_cm"), Location.ToString());
			Result->SetStringField(TEXT("rotation_degrees"), Rotation.ToString());
		}
		return Result;
	}

#if !UE_BUILD_SHIPPING
	static FAutoConsoleCommandWithWorldAndArgs Start(
		TEXT("sw.Insights.Start"), TEXT("sw.Insights.Start <label> [seconds=20]: save a bounded trace and settings JSON."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USWInsightsCaptureSubsystem* Subsystem = World ? World->GetSubsystem<USWInsightsCaptureSubsystem>() : nullptr)
			{
				float Seconds = 20.0f;
				if (Args.Num() > 1 && !LexTryParseString(Seconds, *Args[1]))
				{
					UE_LOG(LogSWInsights, Warning, TEXT("Seconds must be a number in (0, 300]."));
					return;
				}
				Subsystem->StartCapture(Args.IsEmpty() ? TEXT("Capture") : Args[0], Seconds);
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs Mark(
		TEXT("sw.Insights.Mark"), TEXT("sw.Insights.Mark <label>: bookmark the current camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USWInsightsCaptureSubsystem* Subsystem = World ? World->GetSubsystem<USWInsightsCaptureSubsystem>() : nullptr)
			{
				Subsystem->MarkCapture(FString::Join(Args, TEXT(" ")));
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs Stop(
		TEXT("sw.Insights.Stop"), TEXT("Stop only the trace owned by this world's SW capture."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (USWInsightsCaptureSubsystem* Subsystem = World ? World->GetSubsystem<USWInsightsCaptureSubsystem>() : nullptr)
			{
				Subsystem->StopCapture();
			}
		}));
#endif
}

bool USWInsightsCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return !UE_BUILD_SHIPPING && World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void USWInsightsCaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FString Label;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SWInsightsCapture="), Label))
	{
		return;
	}
	float Seconds = 20.0f;
	float Warmup = 10.0f;
	FParse::Value(FCommandLine::Get(), TEXT("SWInsightsSeconds="), Seconds);
	FParse::Value(FCommandLine::Get(), TEXT("SWInsightsWarmup="), Warmup);
	bAutoQuit = InWorld.WorldType == EWorldType::Game && FParse::Param(FCommandLine::Get(), TEXT("SWInsightsAutoQuit"));
	ScheduledCapture = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this, Label, Seconds](float)
		{
			ScheduledCapture.Reset();
			if (!StartCapture(Label, Seconds) && bAutoQuit)
			{
				FPlatformMisc::RequestExit(false);
			}
			return false;
		}), FMath::IsFinite(Warmup) ? FMath::Clamp(Warmup, 0.0f, 300.0f) : 10.0f);
}

bool USWInsightsCaptureSubsystem::StartCapture(const FString& Label, float Seconds)
{
	check(IsInGameThread());
	if (!FMath::IsFinite(Seconds) || Seconds <= 0.0f || Seconds > 300.0f)
	{
		UE_LOG(LogSWInsights, Warning, TEXT("Seconds must be in (0, 300]."));
		return false;
	}
	if (SWInsights::CaptureOwner.IsValid() || FTraceAuxiliary::IsConnected())
	{
		UE_LOG(LogSWInsights, Warning, TEXT("Another trace is active; it will not be replaced. Use Trace.Status to inspect it, or Trace.Stop to finish it before starting this capture."));
		return false;
	}
	FString SafeLabel;
	for (const TCHAR Character : Label.Left(64))
	{
		SafeLabel.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-') ? Character : TEXT('_'));
	}
	if (SafeLabel.IsEmpty()) { SafeLabel = TEXT("Capture"); }
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profiling/Insights"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	TracePath = Directory / FString::Printf(TEXT("%s_%s_%s.utrace"), *SafeLabel,
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Metadata = MakeShared<FJsonObject>();
	Metadata->SetStringField(TEXT("label"), Label);
	Metadata->SetStringField(TEXT("trace"), TracePath);
	Metadata->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
	Metadata->SetStringField(TEXT("map"), GetWorld()->GetMapName());
	Metadata->SetNumberField(TEXT("net_mode"), static_cast<int32>(GetWorld()->GetNetMode()));
	Metadata->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand());
	Metadata->SetStringField(TEXT("gpu"), FPlatformMisc::GetPrimaryGPUBrand());
	Metadata->SetStringField(TEXT("channels"), SWInsights::Channels);
	Metadata->SetNumberField(TEXT("requested_seconds"), Seconds);
	Metadata->SetObjectField(TEXT("camera_start"), SWInsights::Camera(GetWorld()));
	TSharedRef<FJsonObject> CVars = MakeShared<FJsonObject>();
	for (const TCHAR* Name : {TEXT("r.Nanite"), TEXT("Landscape.RenderNanite"), TEXT("r.ScreenPercentage"),
		TEXT("r.VSync"), TEXT("t.MaxFPS"), TEXT("r.RayTracing.ResidentGeometryMemoryPoolSizeInMB"),
		TEXT("r.ViewDistanceScale"), TEXT("grass.DensityScale"), TEXT("foliage.DensityScale"),
		TEXT("grass.DisableDynamicShadows"), TEXT("r.ContactShadows"),
		TEXT("r.Shadow.Virtual.Enable"), TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages"),
		TEXT("p.ShowCabinSwimCullDebug"), TEXT("sw.ShipWake.OnScreenDebug")})
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVars->SetStringField(Name, Variable->GetString());
		}
	}
	Metadata->SetObjectField(TEXT("cvars"), CVars);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		Metadata->SetNumberField(TEXT("viewport_width"), Size.X);
		Metadata->SetNumberField(TEXT("viewport_height"), Size.Y);
	}
	TArray<TSharedPtr<FJsonValue>> Landscapes;
	for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actor"), It->GetPathName());
		TArray<ULandscapeNaniteComponent*> Components;
		It->GetComponents(Components);
		Entry->SetNumberField(TEXT("nanite_components"), Components.Num());
#if WITH_EDITOR
		Entry->SetBoolField(TEXT("nanite_enabled"), It->IsNaniteEnabled());
		Entry->SetBoolField(TEXT("nanite_up_to_date"), It->IsNaniteMeshUpToDate());
#endif
		Landscapes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Metadata->SetArrayField(TEXT("landscapes"), Landscapes);
	FTraceAuxiliary::FOptions Options;
	Options.bExcludeTail = true;
	TStringBuilder<512> EnabledChannels;
	FTraceAuxiliary::GetActiveChannelsString(EnabledChannels);
	TArray<FString> PreviousChannels, RequestedChannels, NewChannels;
	FString(EnabledChannels.ToString()).ParseIntoArray(PreviousChannels, TEXT(","));
	FString(SWInsights::Channels).ParseIntoArray(RequestedChannels, TEXT(","));
	for (const FString& Channel : RequestedChannels)
	{
		if (!PreviousChannels.ContainsByPredicate([&Channel](const FString& Existing)
			{ return Existing.Equals(Channel, ESearchCase::IgnoreCase); }))
		{
			NewChannels.Add(Channel);
		}
	}
	AddedChannels = FString::Join(NewChannels, TEXT(","));
	if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TracePath, SWInsights::Channels, &Options))
	{
		UE_LOG(LogSWInsights, Error, TEXT("Could not start trace: %s"), *TracePath);
		return false;
	}
	SWInsights::CaptureOwner = this;
	++GCycleStatsShouldEmitNamedEvents;
	bAddedNamedEvents = true;
	CaptureStartSeconds = FPlatformTime::Seconds();
	WriteMetadata();
	TRACE_BOOKMARK(TEXT("SW Capture Begin %s"), *Label);
	UE_LOG(LogSWInsights, Display, TEXT("Capture started for %.1fs: %s"), Seconds, *TracePath);
	FTSTicker::GetCoreTicker().RemoveTicker(ScheduledCapture);
	ScheduledCapture = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this](float)
		{
			ScheduledCapture.Reset();
			StopCapture();
			if (bAutoQuit) { FPlatformMisc::RequestExit(false); }
			return false;
		}), Seconds);
	return true;
}

bool USWInsightsCaptureSubsystem::OwnsConnection() const
{
	// The writer connects asynchronously; its GUID is not available immediately after Start.
	// A unique destination identifies our request even before the writer thread connects.
	return SWInsights::CaptureOwner.Get() == this
		&& FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File
		&& FPaths::IsSamePath(FTraceAuxiliary::GetTraceDestinationString(), TracePath);
}

void USWInsightsCaptureSubsystem::MarkCapture(const FString& Label)
{
	if (!OwnsConnection()) { return; }
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->GetPlayerViewPoint(Location, Rotation); }
	TRACE_BOOKMARK(TEXT("SW Mark %s Camera=%s Rotation=%s"), *Label, *Location.ToString(), *Rotation.ToString());
}

void USWInsightsCaptureSubsystem::StopCapture()
{
	FTSTicker::GetCoreTicker().RemoveTicker(ScheduledCapture);
	ScheduledCapture.Reset();
	if (SWInsights::CaptureOwner.Get() != this) { return; }
	const bool bOwned = OwnsConnection();
	if (bOwned)
	{
		TRACE_BOOKMARK(TEXT("SW Capture End"));
		FTraceAuxiliary::Stop();
		if (!AddedChannels.IsEmpty()) { FTraceAuxiliary::DisableChannels(*AddedChannels); }
	}
	if (bAddedNamedEvents) { --GCycleStatsShouldEmitNamedEvents; bAddedNamedEvents = false; }
	SWInsights::CaptureOwner.Reset();
	Metadata->SetBoolField(TEXT("stopped_own_trace"), bOwned);
	Metadata->SetNumberField(TEXT("elapsed_wall_seconds"), FPlatformTime::Seconds() - CaptureStartSeconds);
	Metadata->SetObjectField(TEXT("camera_end"), SWInsights::Camera(GetWorld()));
	WriteMetadata();
	UE_LOG(LogSWInsights, Display, TEXT("Capture finished: %s"), *TracePath);
}

void USWInsightsCaptureSubsystem::WriteMetadata() const
{
	FString Json;
	FJsonSerializer::Serialize(Metadata.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
	if (!FFileHelper::SaveStringToFile(Json, *FPaths::ChangeExtension(TracePath, TEXT("json"))))
	{
		UE_LOG(LogSWInsights, Warning, TEXT("Could not write capture metadata: %s"), *TracePath);
	}
}

void USWInsightsCaptureSubsystem::Deinitialize()
{
	StopCapture();
	Super::Deinitialize();
}
