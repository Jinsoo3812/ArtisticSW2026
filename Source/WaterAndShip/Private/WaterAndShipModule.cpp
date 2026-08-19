#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FWaterAndShipModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Project/Shaders"), ShaderDir);
	}

	virtual void ShutdownModule() override
	{
		ResetAllShaderSourceDirectoryMappings();
	}
};

IMPLEMENT_MODULE(FWaterAndShipModule, WaterAndShip);
