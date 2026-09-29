#include "Modules/ModuleManager.h"
	 
#include "Network/SWNetworkLog.h"
#include "Misc/OutputDeviceRedirector.h"

class FArtisticSWCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ConnectionLog = MakeUnique<FSWConnectionFileOutputDevice>();
		if (GLog && ConnectionLog->IsOpen()) GLog->AddOutputDevice(ConnectionLog.Get());
		RoomFlowLog = MakeUnique<FSWConnectionFileOutputDevice>(true);
		if (GLog && RoomFlowLog->IsOpen()) GLog->AddOutputDevice(RoomFlowLog.Get());
	}

	virtual void ShutdownModule() override
	{
		if (GLog && ConnectionLog) GLog->RemoveOutputDevice(ConnectionLog.Get());
		if (GLog && RoomFlowLog) GLog->RemoveOutputDevice(RoomFlowLog.Get());
		ConnectionLog.Reset();
		RoomFlowLog.Reset();
	}

private:
	TUniquePtr<FSWConnectionFileOutputDevice> ConnectionLog;
	TUniquePtr<FSWConnectionFileOutputDevice> RoomFlowLog;
};

IMPLEMENT_MODULE(FArtisticSWCoreModule, ArtisticSWCore);
