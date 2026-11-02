#include "SWApplyTerrainProfileCommandlet.h"

#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "LandscapeProxy.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

USWApplyTerrainProfileCommandlet::USWApplyTerrainProfileCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USWApplyTerrainProfileCommandlet::Main(const FString& Params)
{
	const FName TerrainName(TEXT("지형"));
	FCollisionResponseTemplate BaseProfile;
	FCollisionResponseTemplate TerrainProfile;
	if (!UCollisionProfile::Get()->GetProfileTemplate(TEXT("BlockAll"), BaseProfile)
		|| !UCollisionProfile::Get()->GetProfileTemplate(TerrainName, TerrainProfile)) return 1;
	if (BaseProfile.ObjectType != TerrainProfile.ObjectType || BaseProfile.CollisionEnabled != TerrainProfile.CollisionEnabled) return 2;
	int32 Differences = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseProfile.ResponseToChannels.EnumArray); ++Index)
	{
		const ECollisionChannel Channel = static_cast<ECollisionChannel>(Index);
		const bool bCannon = Channel == ECC_GameTraceChannel2 || Channel == ECC_GameTraceChannel3;
		const ECollisionResponse Expected = bCannon ? ECR_Block : BaseProfile.ResponseToChannels.GetResponse(Channel);
		if (TerrainProfile.ResponseToChannels.GetResponse(Channel) != Expected) return 3;
		Differences += TerrainProfile.ResponseToChannels.GetResponse(Channel) != BaseProfile.ResponseToChannels.GetResponse(Channel);
	}
	UE_LOG(LogTemp, Display, TEXT("[TerrainProfile] Profile=%s Base=BlockAll Differences=%d PlayerCannon=%d EnemyCannon=%d"),
		*TerrainName.ToString(), Differences, TerrainProfile.ResponseToChannels.GetResponse(ECC_GameTraceChannel2),
		TerrainProfile.ResponseToChannels.GetResponse(ECC_GameTraceChannel3));

	const FString MapPackage(TEXT("/Game/Level/Lvl_CY"));
	const FString MapFilename = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	if (!World) return 4;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	TArray<UPackage*> Packages;
	int32 Landscapes = 0;
	int32 Components = 0;
	bool bValid = true;
	FProperty* BodyProperty = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(ALandscapeProxy, BodyInstance));
	if (!BodyProperty) return 5;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		++Landscapes;
		UE_LOG(LogTemp, Display, TEXT("[TerrainProfile] Before Actor=%s Package=%s Profile=%s PlayerCannon=%d EnemyCannon=%d"),
			*Landscape->GetPathName(), *Landscape->GetPackage()->GetName(), *Landscape->BodyInstance.GetCollisionProfileName().ToString(),
			Landscape->BodyInstance.GetResponseToChannel(ECC_GameTraceChannel2), Landscape->BodyInstance.GetResponseToChannel(ECC_GameTraceChannel3));
		if (bApply && Landscape->BodyInstance.GetCollisionProfileName() != TerrainName)
		{
			Landscape->Modify();
			Landscape->BodyInstance.SetCollisionProfileName(TerrainName);
			FPropertyChangedEvent Change(BodyProperty, EPropertyChangeType::ValueSet);
			Change.MemberProperty = BodyProperty;
			Landscape->PostEditChangeProperty(Change);
			Landscape->MarkPackageDirty();
			Packages.AddUnique(Landscape->GetPackage());
		}
		bValid &= Landscape->BodyInstance.GetCollisionProfileName() == TerrainName
			&& Landscape->BodyInstance.GetResponseToChannel(ECC_GameTraceChannel2) == ECR_Block
			&& Landscape->BodyInstance.GetResponseToChannel(ECC_GameTraceChannel3) == ECR_Block;
		TInlineComponentArray<ULandscapeHeightfieldCollisionComponent*> CollisionComponents(Landscape);
		for (ULandscapeHeightfieldCollisionComponent* Component : CollisionComponents)
		{
			++Components;
			if (bApply) Component->RecreatePhysicsState();
			bValid &= Component->GetCollisionObjectType() == ECC_WorldStatic
				&& Component->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics
				&& Component->GetCollisionResponseToChannel(ECC_GameTraceChannel2) == ECR_Block
				&& Component->GetCollisionResponseToChannel(ECC_GameTraceChannel3) == ECR_Block;
		}
	}
	if (!bValid || Landscapes == 0 || Components == 0) return 6;
	if (bApply && !Packages.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Packages, true)) return 7;
	UE_LOG(LogTemp, Display, TEXT("[TerrainProfile] Result=%s Map=%s Landscapes=%d CollisionComponents=%d SavedPackages=%d"),
		bApply ? TEXT("Applied") : TEXT("Verified"), *MapPackage, Landscapes, Components, Packages.Num());
	return 0;
}
