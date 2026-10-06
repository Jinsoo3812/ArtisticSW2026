#include "Room/SWVoyageResetProfile.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

USWVoyageResetProfile::USWVoyageResetProfile()
{
}

bool USWVoyageResetProfile::ValidateProfile(FString& OutError) const
{
	OutError.Reset();
	if (GameplayLevel.IsNull()) { OutError = TEXT("VoyageGameplayLevelMissing"); return false; }
	if (!FPackageName::IsValidLongPackageName(GameplayLevel.ToSoftObjectPath().GetLongPackageName()))
	{
		OutError = TEXT("VoyageGameplayLevelPathInvalid"); return false;
	}
	const float Timeouts[] = { PresentationTimeoutSeconds, StreamingTimeoutSeconds, RestoreTimeoutSeconds, ClientReadyTimeoutSeconds, TotalTimeoutSeconds };
	for (float Timeout : Timeouts)
	{
		if (!FMath::IsFinite(Timeout) || Timeout <= 0.f) { OutError = TEXT("VoyageTimeoutInvalid"); return false; }
	}
	TSet<FName> UniquePackages;
	for (FName Package : ProjectScriptPackages)
	{
		if (!Package.ToString().StartsWith(TEXT("/Script/")) || UniquePackages.Contains(Package))
		{
			OutError = TEXT("VoyageProjectScriptPackageInvalid"); return false;
		}
		UniquePackages.Add(Package);
	}
	if (UniquePackages.IsEmpty()) { OutError = TEXT("VoyageProjectScriptPackagesMissing"); return false; }
	return true;
}
