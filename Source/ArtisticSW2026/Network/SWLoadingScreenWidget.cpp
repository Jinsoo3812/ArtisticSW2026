#include "Network/SWLoadingScreenWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"

void USWLoadingScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BackgroundImage) DefaultBackground = BackgroundImage->GetBrush();
	if (LoadingMessageText) DefaultMessage = LoadingMessageText->GetText();
}

FString USWLoadingScreenWidget::NormalizeMapName(const FString& MapName)
{
	FString Package = MapName;
	int32 OptionIndex;
	if (Package.FindChar(TEXT('?'), OptionIndex)) Package.LeftInline(OptionIndex);
	Package = FPackageName::ObjectPathToPackageName(Package);
	const FString Path = FPackageName::GetLongPackagePath(Package);
	FString Name = FPackageName::GetShortName(Package);
	if (Name.StartsWith(TEXT("UEDPIE_")))
	{
		const int32 PrefixEnd = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (PrefixEnd != INDEX_NONE) Name.RightChopInline(PrefixEnd + 1);
	}
	return Path.IsEmpty() ? Name : Path / Name;
}

void USWLoadingScreenWidget::SetDestination(const FString& MapName)
{
	// Always restore defaults so a previous destination cannot leak into the next trip.
	if (BackgroundImage) BackgroundImage->SetBrush(DefaultBackground);
	if (LoadingMessageText) LoadingMessageText->SetText(DefaultMessage);
	const FString Destination = NormalizeMapName(MapName);
	if (Destination.IsEmpty()) return;
	for (const FSWLoadingDestination& Entry : Destinations)
	{
		if (Entry.Level.IsNull() || NormalizeMapName(Entry.Level.ToSoftObjectPath().GetLongPackageName()) != Destination) continue;
		if (BackgroundImage && Entry.Background) BackgroundImage->SetBrushFromTexture(Entry.Background, false);
		if (LoadingMessageText && !Entry.Message.IsEmpty()) LoadingMessageText->SetText(Entry.Message);
		break;
	}
}

void USWLoadingScreenWidget::SetStatus(const FText& Status)
{
	if (LoadingStatusText) LoadingStatusText->SetText(Status);
}
