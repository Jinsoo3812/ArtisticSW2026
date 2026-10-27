#include "Animation/SWFoleyMigrationLibrary.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/SWAnimNotify_FoleyEvent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
bool ReadPayload(UAnimNotify* Notify, FSWFoleyEvent& Out, FString& Error)
{
	const FStructProperty* Tag = FindFProperty<FStructProperty>(Notify->GetClass(), TEXT("Event"));
	const FProperty* Side = FindFProperty<FProperty>(Notify->GetClass(), TEXT("Side"));
	const FNumericProperty* Volume = FindFProperty<FNumericProperty>(Notify->GetClass(), TEXT("VolumeMultiplier"));
	const FNumericProperty* Pitch = FindFProperty<FNumericProperty>(Notify->GetClass(), TEXT("PitchMultiplier"));
	if (!Tag || Tag->Struct != FGameplayTag::StaticStruct() || !Side || !Volume || !Pitch || !Volume->IsFloatingPoint() || !Pitch->IsFloatingPoint())
	{
		Error = TEXT("Unrecognized legacy property schema"); return false;
	}
	Out.Event = *Tag->ContainerPtrToValuePtr<FGameplayTag>(Notify);
	Out.VolumeMultiplier = float(Volume->GetFloatingPointPropertyValue(Volume->ContainerPtrToValuePtr<void>(Notify)));
	Out.PitchMultiplier = float(Pitch->GetFloatingPointPropertyValue(Pitch->ContainerPtrToValuePtr<void>(Notify)));
	UEnum* Enum = nullptr;
	int64 Value = -1;
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Side))
	{
		Enum = EnumProperty->GetEnum();
		Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Notify));
	}
	else if (const FByteProperty* Byte = CastField<FByteProperty>(Side))
	{
		Enum = Byte->Enum; Value = Byte->GetPropertyValue_InContainer(Notify);
	}
	const FString Label = Enum ? Enum->GetDisplayNameTextByValue(Value).ToString() : FString();
	if (Label == TEXT("None")) { Out.Side = ESWFoleySide::None; }
	else if (Label == TEXT("Left")) { Out.Side = ESWFoleySide::Left; }
	else if (Label == TEXT("Right")) { Out.Side = ESWFoleySide::Right; }
	else { Error = TEXT("Unknown authored Side: ") + Label; return false; }
	if (!Out.Event.IsValid() || !FMath::IsFinite(Out.VolumeMultiplier) || !FMath::IsFinite(Out.PitchMultiplier) ||
		Out.VolumeMultiplier < 0.f || Out.PitchMultiplier <= 0.f || Out.Event.ToString().Contains(TEXT("Slide.Loop")))
	{
		Error = TEXT("Invalid or unsupported looping payload"); return false;
	}
	return true;
}

FString ExportEvent(const FAnimNotifyEvent& Event)
{
	FString Text;
	FAnimNotifyEvent::StaticStruct()->ExportText(Text, &Event, nullptr, nullptr, PPF_None, nullptr);
	return Text;
}


struct FReadOnlySourceMount
{
    FString Notifies, Content;
    explicit FReadOnlySourceMount(const FString& Source)
        : Notifies(FPaths::Combine(Source, TEXT("Blueprints/AnimNotifies")) + TEXT("/")), Content(Source + TEXT("/"))
    {
        // Narrow mount: never hide the target's Player Blueprints or existing GASP audio bank.
        FPackageName::RegisterMountPoint(TEXT("/Game/Blueprints/AnimNotifies/"), Notifies);
        FPackageName::RegisterMountPoint(TEXT("/FoleySource/"), Content);
    }
    ~FReadOnlySourceMount()
    {
        FPackageName::UnRegisterMountPoint(TEXT("/FoleySource/"), Content);
        FPackageName::UnRegisterMountPoint(TEXT("/Game/Blueprints/AnimNotifies/"), Notifies);
    }
};
}

FString USWFoleyMigrationLibrary::MigrateGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply)
{
    check(IsInGameThread());
    const auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Results;
    int32 Errors = 0, Replacements = 0, Recovered = 0, Saved = 0;
    auto Fail = [&](const FString& Path, const FString& Reason)
    {
        const auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Path); Row->SetStringField(TEXT("error"), Reason);
        Results.Add(MakeShared<FJsonValueObject>(Row)); ++Errors;
    };
    if (FPaths::IsRelative(SourceContent) ||
        !IFileManager::Get().DirectoryExists(*FPaths::Combine(SourceContent, TEXT("Blueprints/AnimNotifies"))) ||
        IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Blueprints/AnimNotifies"))) ||
        FPackageName::MountPointExists(TEXT("/FoleySource/")))
    {
        Fail(TEXT("source"), TEXT("Missing source or an occupied notify mount; refusing to shadow authored assets"));
    }
    else
    {
        FReadOnlySourceMount Mount(SourceContent);
        TStrongObjectPtr<UClass> Legacy(LoadClass<UAnimNotify>(nullptr,
            TEXT("/Game/Blueprints/AnimNotifies/BP_AnimNotify_FoleyEvent.BP_AnimNotify_FoleyEvent_C")));
        if (!Legacy.IsValid()) { Fail(TEXT("source"), TEXT("Cannot load GASP Foley class")); }
        else for (const FString& Path : Packages)
        {
            if (!Path.StartsWith(TEXT("/Game/Characters/UEFN_Mannequin/Animations/")) || Path.Contains(TEXT("..")))
            { Fail(Path, TEXT("Outside imported-animation root")); break; }
            UPackage* Existing = FindPackage(nullptr, *Path);
            if (Existing && Existing->IsDirty()) { Fail(Path, TEXT("Unsaved target changes")); break; }
            const FString Suffix = TEXT(".") + FPackageName::GetShortName(Path);
            TStrongObjectPtr<UAnimSequenceBase> Target(LoadObject<UAnimSequenceBase>(nullptr, *(Path + Suffix)));
            if (!Target.IsValid()) { Fail(Path, TEXT("Cannot load target animation")); break; }
            TStrongObjectPtr<UAnimSequenceBase> Source;
            TArray<TPair<int32, FSWFoleyEvent>> Planned;
            TArray<TSharedPtr<FJsonValue>> Events, Before, After;
            bool bValid = true;
            for (int32 Index = 0; Index < Target->Notifies.Num(); ++Index)
            {
                const FAnimNotifyEvent& Event = Target->Notifies[Index];
                Before.Add(MakeShared<FJsonValueString>(ExportEvent(Event)));
                if (Event.Notify && Event.Notify->IsA<USWAnimNotify_FoleyEvent>()) { continue; }
                UAnimNotify* PayloadObject = Event.Notify;
                bool bRecovery = false;
                if (!Event.Notify && !Event.NotifyStateClass && Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent")))
                {
                    if (!Source.IsValid())
                    {
                        const FString SourcePath = TEXT("/FoleySource/") + Path.RightChop(6);
                        Source.Reset(LoadObject<UAnimSequenceBase>(nullptr, *(SourcePath + Suffix)));
                    }
                    if (!Source.IsValid()) { Fail(Path, TEXT("Cannot read source animation for stripped contact")); bValid = false; break; }
                    const FAnimNotifyEvent* Original = Source->Notifies.FindByPredicate([&](const FAnimNotifyEvent& Value) { return Value.Guid == Event.Guid; });
                    if (!Event.Guid.IsValid() || !Original) { Fail(Path, TEXT("Missing source contact GUID")); bValid = false; break; }
                    FAnimNotifyEvent Normalized = *Original; Normalized.Notify = nullptr;
                    if (ExportEvent(Normalized).Replace(TEXT("/FoleySource/"), TEXT("/Game/")) != ExportEvent(Event))
                    { Fail(Path, TEXT("Stripped contact differs from original metadata; manual review required")); bValid = false; break; }
                    if (!Original->Notify) { continue; } // Preserve genuine named-only GASP markers.
                    PayloadObject = Original->Notify; bRecovery = true;
                }
                if (!PayloadObject || !PayloadObject->IsA(Legacy.Get())) { continue; }
                FSWFoleyEvent Payload; FString Error;
                if (!ReadPayload(PayloadObject, Payload, Error)) { Fail(Path, Error); bValid = false; break; }
                Planned.Emplace(Index, Payload);
                const auto Row = MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("index"), Index); Row->SetStringField(TEXT("guid"), Event.Guid.ToString());
                Row->SetNumberField(TEXT("time"), Event.GetTime()); Row->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
                Row->SetStringField(TEXT("event"), Payload.Event.ToString()); Row->SetNumberField(TEXT("side"), int32(Payload.Side));
                Row->SetNumberField(TEXT("volume"), Payload.VolumeMultiplier); Row->SetNumberField(TEXT("pitch"), Payload.PitchMultiplier);
                Row->SetBoolField(TEXT("recovered"), bRecovery);
                Events.Add(MakeShared<FJsonValueObject>(Row));
                Recovered += bRecovery;
            }
            if (!bValid) { break; }
            if (bApply && !Planned.IsEmpty())
            {
                Target->Modify();
                for (const auto& Plan : Planned)
                {
                    FAnimNotifyEvent& Event = Target->Notifies[Plan.Key];
                    const FString Metadata = ExportEvent(Event);
                    UAnimNotify* Old = Event.Notify;
                    auto* Native = NewObject<USWAnimNotify_FoleyEvent>(Target.Get(), NAME_None, RF_Transactional);
                    Native->FoleyEvent = Plan.Value;
                    if (Old) { Native->NotifyColor = Old->NotifyColor; }
                    Event.Notify = Native;
                    FAnimNotifyEvent Normalized = Event; Normalized.Notify = Old;
                    checkf(Metadata == ExportEvent(Normalized), TEXT("Contact migration changed event metadata"));
                    Event.bTriggerOnDedicatedServer = false;
                    if (Old) { Old->ClearFlags(RF_Public | RF_Standalone); Old->SetFlags(RF_Transient); }
                }
                Target->RefreshCacheData(); Target->MarkPackageDirty();
                FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
                const FString File = FPaths::Combine(FPaths::ProjectContentDir(), Path.RightChop(6)) + FPackageName::GetAssetPackageExtension();
                if (!UPackage::SavePackage(Target->GetPackage(), Target.Get(), *File, Args))
                { Fail(Path, TEXT("Save failed; restore the backed-up target")); break; }
                ++Saved;
            }
            for (const auto& Event : Target->Notifies) { After.Add(MakeShared<FJsonValueString>(ExportEvent(Event))); }
            const auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("package"), Path); Item->SetArrayField(TEXT("events"), Events);
            Item->SetArrayField(TEXT("before"), Before); Item->SetArrayField(TEXT("after"), After);
            Item->SetNumberField(TEXT("notify_count"), Target->Notifies.Num());
            Results.Add(MakeShared<FJsonValueObject>(Item)); Replacements += Planned.Num();
        }
    }
    Report->SetNumberField(TEXT("errors"), Errors); Report->SetNumberField(TEXT("replacements"), Replacements);
    Report->SetNumberField(TEXT("recovered"), Recovered); Report->SetNumberField(TEXT("saved"), Saved);
    Report->SetArrayField(TEXT("packages"), Results);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json)); return Json;
}

TArray<FString> USWFoleyMigrationLibrary::ExportNotifyMetadata(UAnimSequenceBase* Sequence)
{
    TArray<FString> Result;
    if (Sequence) { for (const auto& Event : Sequence->Notifies) { Result.Add(ExportEvent(Event)); } }
    return Result;
}
