#pragma once

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

namespace SWEnemyShipISMMaterials
{
inline FString Folder() { return TEXT("/Game/Blueprints/Ship/Enemy_Ship/Materials/ISM"); }
inline FString AssetName(const FString& OriginalPath)
{
	return TEXT("ISM_") + FMD5::HashAnsiString(*OriginalPath);
}
inline FString ObjectPath(const FString& OriginalPath)
{
	if (OriginalPath.StartsWith(Folder() + TEXT("/"))) return OriginalPath;
	const FString Name = AssetName(OriginalPath);
	return Folder() + TEXT("/") + Name + TEXT(".") + Name;
}
}
