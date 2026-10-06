#include "Modules/ModuleManager.h"
	 
#include "Network/SWNetworkLog.h"
#include "Room/SWVoyageSpawnLibrary.h"
#include "Network/SWRoomLoadDiagnostics.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDeviceRedirector.h"
#include "UObject/UObjectGlobals.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Containers/Ticker.h"

class FArtisticSWCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (SWRoomLoadDiagnostics::IsEnabled())
		{
			SWRoomLoadDiagnostics::Mark(TEXT("CoreModule.Startup"));
			PreLoadHandle = FCoreUObjectDelegates::PreLoadMap.AddRaw(this, &FArtisticSWCoreModule::DiagnosticPreLoad);
			PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FArtisticSWCoreModule::DiagnosticPostLoad);
			PreInitHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld*, const UWorld::InitializationValues) { SWRoomLoadDiagnostics::Mark(TEXT("World.PreInit")); });
			PostInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld*, const UWorld::InitializationValues) { SWRoomLoadDiagnostics::Mark(TEXT("World.PostInit")); });
			ActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddLambda([](const UWorld::FActorsInitializedParams&) { SWRoomLoadDiagnostics::Mark(TEXT("World.ActorsInitialized")); });
			CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld*, bool, bool) { SWRoomLoadDiagnostics::Mark(TEXT("World.CleanupBegin")); });
			PostCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddLambda([](UWorld*, bool, bool) { SWRoomLoadDiagnostics::Mark(TEXT("World.CleanupEnd")); });
			PreGCHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda([] { SWRoomLoadDiagnostics::Mark(TEXT("GC.Begin")); });
			PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([] { SWRoomLoadDiagnostics::Mark(TEXT("GC.End")); });
		}
		if (IsRunningCommandlet()) return;
		ConnectionLog = MakeUnique<FSWConnectionFileOutputDevice>();
		if (GLog && ConnectionLog->IsOpen()) GLog->AddOutputDevice(ConnectionLog.Get());
		RoomFlowLog = MakeUnique<FSWConnectionFileOutputDevice>(true);
		if (GLog && RoomFlowLog->IsOpen()) GLog->AddOutputDevice(RoomFlowLog.Get());
		if (SWRoomLogging::IsDetailedEnabled())
		{
			SaveTraceLog = MakeUnique<FSWConnectionFileOutputDevice>(false, true);
			if (GLog && SaveTraceLog->IsOpen()) GLog->AddOutputDevice(SaveTraceLog.Get());
		}
		LogDrainHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			if (ConnectionLog) ConnectionLog->FlushBuffered();
			if (RoomFlowLog) RoomFlowLog->FlushBuffered();
			if (SaveTraceLog) SaveTraceLog->FlushBuffered();
			return true;
		}), 1.0f);
	}

	virtual void ShutdownModule() override
	{
		FSWVoyageSpawn::ShutdownSpawnTracking();
		FTSTicker::GetCoreTicker().RemoveTicker(LogDrainHandle);
		FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadHandle);
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
		FWorldDelegates::OnPreWorldInitialization.Remove(PreInitHandle);
		FWorldDelegates::OnPostWorldInitialization.Remove(PostInitHandle);
		FWorldDelegates::OnWorldInitializedActors.Remove(ActorsHandle);
		FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
		FWorldDelegates::OnPostWorldCleanup.Remove(PostCleanupHandle);
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreGCHandle);
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
		if (GLog && ConnectionLog) GLog->RemoveOutputDevice(ConnectionLog.Get());
		if (GLog && RoomFlowLog) GLog->RemoveOutputDevice(RoomFlowLog.Get());
		if (GLog && SaveTraceLog) GLog->RemoveOutputDevice(SaveTraceLog.Get());
		ConnectionLog.Reset();
		RoomFlowLog.Reset();
		SaveTraceLog.Reset();
	}

private:
	void DiagnosticPreLoad(const FString& Map)
	{
		MapLoadStartedAt = FPlatformTime::Seconds();
		MapRegion = FString::Printf(TEXT("SW.MapLoad.%d"), ++MapSerial);
		TRACE_BEGIN_REGION(*MapRegion);
		SWRoomLoadDiagnostics::MarkMemory(TEXT("Map.PreLoad"));
		UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Pid=%u Real=%.6f Phase=Map.PreLoad Map=%s"),
			FPlatformProcess::GetCurrentProcessId(), MapLoadStartedAt, *Map);
	}
	void DiagnosticPostLoad(UWorld* World)
	{
		const double Now = FPlatformTime::Seconds();
		TRACE_END_REGION(*MapRegion);
		UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Pid=%u Real=%.6f Phase=Map.PostLoad Map=%s NetMode=%d ElapsedMs=%.3f"),
			FPlatformProcess::GetCurrentProcessId(), Now, World ? *World->GetMapName() : TEXT("Failed"),
			World ? static_cast<int32>(World->GetNetMode()) : -1, MapLoadStartedAt > 0.0 ? (Now - MapLoadStartedAt) * 1000.0 : -1.0);
		SWRoomLoadDiagnostics::MarkMemory(TEXT("Map.PostLoad"));
	}
	double MapLoadStartedAt = 0.0;
	int32 MapSerial = 0;
	FString MapRegion;
	FDelegateHandle PreInitHandle, PostInitHandle, ActorsHandle, CleanupHandle, PostCleanupHandle, PreGCHandle, PostGCHandle;
	FDelegateHandle PreLoadHandle;
	FDelegateHandle PostLoadHandle;
	TUniquePtr<FSWConnectionFileOutputDevice> ConnectionLog;
	TUniquePtr<FSWConnectionFileOutputDevice> RoomFlowLog;
	TUniquePtr<FSWConnectionFileOutputDevice> SaveTraceLog;
	FTSTicker::FDelegateHandle LogDrainHandle;
};

IMPLEMENT_MODULE(FArtisticSWCoreModule, ArtisticSWCore);
