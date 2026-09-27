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
	}

	virtual void ShutdownModule() override
	{
		if (GLog && ConnectionLog) GLog->RemoveOutputDevice(ConnectionLog.Get());
		ConnectionLog.Reset();
	}

private:
	TUniquePtr<FSWConnectionFileOutputDevice> ConnectionLog;
};

IMPLEMENT_MODULE(FArtisticSWCoreModule, ArtisticSWCore);
