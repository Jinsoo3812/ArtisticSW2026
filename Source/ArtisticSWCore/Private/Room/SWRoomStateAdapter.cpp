#include "Room/SWRoomStateAdapter.h"

#include "UObject/UnrealType.h"

namespace
{
void AddDifference(const FProperty* Property, const void* Before, const void* After,
	const FString& Path, TArray<FString>& OutFields)
{
	FString BeforeText, AfterText;
	Property->ExportTextItem_Direct(BeforeText, Before, nullptr, nullptr, PPF_None);
	Property->ExportTextItem_Direct(AfterText, After, nullptr, nullptr, PPF_None);
	OutFields.Add(FString::Printf(TEXT("Field=%s Expected=%s Actual=%s"), *Path, *BeforeText, *AfterText));
}

void CompareProperty(const FProperty* Property, const void* Before, const void* After,
	const FString& Path, float TimeTolerance, TArray<FString>& OutFields);

FString EffectIdentity(const FSWRoomGameplayEffectState& State)
{
	TArray<FString> Values;
	for (const FSWRoomSetByCallerTagValue& Value : State.TagMagnitudes)
		Values.Add(Value.Tag.ToString() + TEXT("=") + FString::SanitizeFloat(Value.Value));
	for (const FSWRoomSetByCallerNameValue& Value : State.NameMagnitudes)
		Values.Add(Value.Name.ToString() + TEXT("=") + FString::SanitizeFloat(Value.Value));
	Values.Sort();
	return State.EffectClass.ToString() + TEXT("|") + State.SourceStableId.ToString() + TEXT("|")
		+ State.SourceClass.ToString() + TEXT("|") + State.CapturedSourceTags.ToStringSimple()
		+ TEXT("|") + FString::FromInt(State.StackCount) + TEXT("|") + FString::Join(Values, TEXT(","));
}

void CompareStruct(const UScriptStruct* Type, const void* Before, const void* After,
	const FString& Path, float TimeTolerance, TArray<FString>& OutFields)
{
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property->HasAnyPropertyFlags(CPF_SaveGame)) continue;
		CompareProperty(Property, Property->ContainerPtrToValuePtr<void>(Before),
			Property->ContainerPtrToValuePtr<void>(After),
			Path.IsEmpty() ? Property->GetName() : Path + TEXT(".") + Property->GetName(),
			TimeTolerance, OutFields);
	}
}

void CompareProperty(const FProperty* Property, const void* Before, const void* After,
	const FString& Path, float TimeTolerance, TArray<FString>& OutFields)
{
	if (const FNumericProperty* Number = CastField<FNumericProperty>(Property); Number && Number->IsFloatingPoint())
	{
		const double BeforeValue = Number->GetFloatingPointPropertyValue(Before);
		const double AfterValue = Number->GetFloatingPointPropertyValue(After);
		const double Tolerance = Path.Contains(TEXT("Remaining")) || Path.Contains(TEXT("NextTick"))
			? TimeTolerance : 0.01;
		if (!FMath::IsFinite(BeforeValue) || !FMath::IsFinite(AfterValue)
			|| FMath::Abs(BeforeValue - AfterValue) > Tolerance)
			AddDifference(Property, Before, After, Path, OutFields);
		return;
	}
	if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
	{
		const FName Name = Struct->Struct->GetFName();
		if (Name == TEXT("Vector"))
		{
			const double Tolerance = Path.Contains(TEXT("Angular")) ? 1.0 : 1.0;
			if (!static_cast<const FVector*>(Before)->Equals(*static_cast<const FVector*>(After), Tolerance))
				AddDifference(Property, Before, After, Path, OutFields);
			return;
		}
		if (Name == TEXT("Rotator"))
		{
			if (!static_cast<const FRotator*>(Before)->Equals(*static_cast<const FRotator*>(After), 0.1))
				AddDifference(Property, Before, After, Path, OutFields);
			return;
		}
		if (Name == TEXT("Transform"))
		{
			const FTransform& A = *static_cast<const FTransform*>(Before);
			const FTransform& B = *static_cast<const FTransform*>(After);
			if (!A.GetLocation().Equals(B.GetLocation(), 1.0)
				|| !A.GetRotation().Equals(B.GetRotation(), FMath::DegreesToRadians(0.1))
				|| !A.GetScale3D().Equals(B.GetScale3D(), 0.01))
				AddDifference(Property, Before, After, Path, OutFields);
			return;
		}
		if (Name == TEXT("GameplayTagContainer"))
		{
			const FGameplayTagContainer& A = *static_cast<const FGameplayTagContainer*>(Before);
			const FGameplayTagContainer& B = *static_cast<const FGameplayTagContainer*>(After);
			if (A.Num() != B.Num() || !A.HasAllExact(B)) AddDifference(Property, Before, After, Path, OutFields);
			return;
		}
		CompareStruct(Struct->Struct, Before, After, Path, TimeTolerance, OutFields);
		return;
	}
	if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper BeforeArray(Array, Before), AfterArray(Array, After);
		if (BeforeArray.Num() != AfterArray.Num())
		{
			OutFields.Add(FString::Printf(TEXT("Field=%s.Count Expected=%d Actual=%d"), *Path, BeforeArray.Num(), AfterArray.Num()));
			return;
		}
		if (const FStructProperty* Element = CastField<FStructProperty>(Array->Inner);
			Element && Element->Struct == FSWRoomGameplayEffectState::StaticStruct())
		{
			TArray<int32> BeforeOrder, AfterOrder;
			for (int32 Index = 0; Index < BeforeArray.Num(); ++Index)
			{
				BeforeOrder.Add(Index);
				AfterOrder.Add(Index);
			}
			auto IdentityAt = [](FScriptArrayHelper& Items, int32 Index)
			{ return EffectIdentity(*reinterpret_cast<const FSWRoomGameplayEffectState*>(Items.GetRawPtr(Index))); };
			BeforeOrder.Sort([&](int32 A, int32 B)
			{
				const FString Left = IdentityAt(BeforeArray, A), Right = IdentityAt(BeforeArray, B);
				return Left == Right
					? reinterpret_cast<const FSWRoomGameplayEffectState*>(BeforeArray.GetRawPtr(A))->DurationRemaining
						< reinterpret_cast<const FSWRoomGameplayEffectState*>(BeforeArray.GetRawPtr(B))->DurationRemaining
					: Left < Right;
			});
			AfterOrder.Sort([&](int32 A, int32 B)
			{
				const FString Left = IdentityAt(AfterArray, A), Right = IdentityAt(AfterArray, B);
				return Left == Right
					? reinterpret_cast<const FSWRoomGameplayEffectState*>(AfterArray.GetRawPtr(A))->DurationRemaining
						< reinterpret_cast<const FSWRoomGameplayEffectState*>(AfterArray.GetRawPtr(B))->DurationRemaining
					: Left < Right;
			});
			TMap<FString, int32> ClassOrdinals;
			for (int32 Index = 0; Index < BeforeOrder.Num(); ++Index)
			{
				const FSWRoomGameplayEffectState& BeforeEffect = *reinterpret_cast<const FSWRoomGameplayEffectState*>(
					BeforeArray.GetRawPtr(BeforeOrder[Index]));
				const FString EffectClass = BeforeEffect.EffectClass.ToString();
				const int32 Ordinal = ClassOrdinals.FindOrAdd(EffectClass)++;
				const FString EffectPath = Path + TEXT(".") + EffectClass + TEXT("#") + FString::FromInt(Ordinal);
				const FString Key = IdentityAt(BeforeArray, BeforeOrder[Index]);
				if (Key != IdentityAt(AfterArray, AfterOrder[Index]))
				{
					OutFields.Add(FString::Printf(TEXT("Field=%s.Identity Expected=%s Actual=%s"),
						*EffectPath, *Key, *IdentityAt(AfterArray, AfterOrder[Index])));
					continue;
				}
				CompareStruct(Element->Struct, BeforeArray.GetRawPtr(BeforeOrder[Index]),
					AfterArray.GetRawPtr(AfterOrder[Index]), EffectPath,
					TimeTolerance, OutFields);
			}
			return;
		}
		if (Path.EndsWith(TEXT("Ids")) || Path.EndsWith(TEXT("Tags")))
		{
			TArray<FString> BeforeValues, AfterValues;
			for (int32 Index = 0; Index < BeforeArray.Num(); ++Index)
			{
				FString Value;
				Array->Inner->ExportTextItem_Direct(Value, BeforeArray.GetRawPtr(Index), nullptr, nullptr, PPF_None);
				BeforeValues.Add(Value);
				Value.Reset();
				Array->Inner->ExportTextItem_Direct(Value, AfterArray.GetRawPtr(Index), nullptr, nullptr, PPF_None);
				AfterValues.Add(Value);
			}
			BeforeValues.Sort(); AfterValues.Sort();
			if (BeforeValues != AfterValues)
				OutFields.Add(FString::Printf(TEXT("Field=%s Expected=%s Actual=%s"),
					*Path, *FString::Join(BeforeValues, TEXT(",")), *FString::Join(AfterValues, TEXT(","))));
			return;
		}
		for (int32 Index = 0; Index < BeforeArray.Num(); ++Index)
			CompareProperty(Array->Inner, BeforeArray.GetRawPtr(Index), AfterArray.GetRawPtr(Index),
				FString::Printf(TEXT("%s[%d]"), *Path, Index), TimeTolerance, OutFields);
		return;
	}
	if (!Property->Identical(Before, After)) AddDifference(Property, Before, After, Path, OutFields);
}
}

bool FSWRoomStructCodec::CompareSaveGameStruct(const UScriptStruct* Type, const void* Expected,
	const void* Actual, float TimeToleranceSeconds, TArray<FString>& OutFields)
{
	CompareStruct(Type, Expected, Actual, FString(), TimeToleranceSeconds, OutFields);
	return OutFields.IsEmpty();
}
