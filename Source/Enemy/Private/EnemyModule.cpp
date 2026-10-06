#include "Modules/ModuleManager.h"
#include "Development/SWEnemyShipDebugTeleport.h"
#include "ShipAI/EnemyShipDebugCommands.h"

class FEnemyModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		SWEnemyShipDebug::GetTeleportHandler().BindStatic(&EnemyShipDebug::ExecuteTeleport);
#endif
	}
	virtual void ShutdownModule() override
	{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		SWEnemyShipDebug::GetTeleportHandler().Unbind();
#endif
	}
};

IMPLEMENT_MODULE(FEnemyModule, Enemy);
