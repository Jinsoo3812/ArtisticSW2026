#include "SWBuildLandscapeNaniteCommandlet.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "LandscapeProxy.h"
#include "LandscapeSubsystem.h"
#include "LandscapeEditTypes.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

USWBuildLandscapeNaniteCommandlet::USWBuildLandscapeNaniteCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USWBuildLandscapeNaniteCommandlet::Main(const FString& Params)
{
	FString Map;
	if (!FParse::Value(*Params, TEXT("Map="), Map) || !Map.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(Map))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=SWBuildLandscapeNanite -Map=/Game/Level/Lvl_CY [-CheckOnly] [-EnableNanite]"));
		return 1;
	}
	const FString Filename = FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension());
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Filename);
	if (!World || World->IsPartitionedWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("This tool requires an existing non-partitioned map: %s"), *Map);
		return 1;
	}
	TArray<ALandscapeProxy*> Proxies;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It) { Proxies.Add(*It); }
	if (Proxies.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("No landscape in %s"), *Map);
		return 1;
	}
	const bool bCheckOnly = FParse::Param(*Params, TEXT("CheckOnly"));
	if (!bCheckOnly)
	{
		const FString BackupDir = FPaths::ProjectSavedDir() / TEXT("Optimization/NaniteBackup")
			/ FGuid::NewGuid().ToString(EGuidFormats::Digits);
		IFileManager::Get().MakeDirectory(*BackupDir, true);
		const FString Backup = BackupDir / FPaths::GetCleanFilename(Filename);
		if (IFileManager::Get().Copy(*Backup, *Filename, false) != COPY_OK)
		{
			UE_LOG(LogTemp, Error, TEXT("Map backup failed; nothing will be saved."));
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("[SW-NANITE] Backup=%s"), *FPaths::ConvertRelativePathToFull(Backup));
		for (ALandscapeProxy* Proxy : Proxies)
		{
			if (FParse::Param(*Params, TEXT("EnableNanite")) && !Proxy->IsNaniteEnabled())
			{
				FBoolProperty* Property = FindFProperty<FBoolProperty>(ALandscapeProxy::StaticClass(), TEXT("bEnableNanite"));
				if (!Property) { return 1; }
				Proxy->Modify();
				Property->SetPropertyValue_InContainer(Proxy, true);
				FPropertyChangedEvent Change(Property, EPropertyChangeType::ValueSet);
				static_cast<AActor*>(Proxy)->PostEditChangeProperty(Change);
			}
		}
		ULandscapeSubsystem* Landscape = World->GetSubsystem<ULandscapeSubsystem>();
		if (!Landscape) { return 1; }
		// UE's synchronous build path waits for its Nanite tasks before returning.
		Landscape->BuildNanite(UE::Landscape::EBuildFlags::WriteFinalLog, Proxies);
	}
	for (const ALandscapeProxy* Proxy : Proxies)
	{
		UE_LOG(LogTemp, Display, TEXT("[SW-NANITE] %s Enabled=%d UpToDate=%d"),
			*Proxy->GetPathName(), Proxy->IsNaniteEnabled(), Proxy->IsNaniteMeshUpToDate());
		if (!Proxy->IsNaniteEnabled() || !Proxy->IsNaniteMeshUpToDate()) { return 1; }
	}
	if (!bCheckOnly && !UEditorLoadingAndSavingUtils::SaveMap(World, Map))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save %s"), *Map);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[SW-NANITE] %s %s"), bCheckOnly ? TEXT("Verified") : TEXT("Saved"), *Map);
	return 0;
}
