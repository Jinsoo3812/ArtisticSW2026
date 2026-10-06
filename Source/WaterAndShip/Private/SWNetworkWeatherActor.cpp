#include "SWNetworkWeatherActor.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"

#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Components/LightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Misc/Compression.h"
#include "Containers/StringConv.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceConstant.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWWeatherDiagnostics, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarWeatherDiagnostics(TEXT("sw.Weather.Diagnostics"), 1,
		TEXT("Weather transition diagnostics: 0=off, 1=compact events and samples every five seconds, 2=verbose per-frame diagnostics."));
	// Only persistent assets and value types; never local MIDs, components, or effect actors.
	const TArray<FName>& WeatherProperties()
	{
		static const TArray<FName> Names = {
			TEXT("InitWeather"), TEXT("NextWeather"), TEXT("NextWeather Buf"),
			TEXT("CurrentParam"), TEXT("NextParam"),
			TEXT("CurrentSunColor"), TEXT("NextSunColor"),
			TEXT("CurrentSkyLightColor"), TEXT("NextSkyLightColor"),
			TEXT("CurrentFogColor"), TEXT("NextFogColor"),
			TEXT("CurrentOverallColor"), TEXT("NextOverallColor"), TEXT("SunVisible")
		};
		return Names;
	}
	const TArray<FName>& WeatherMaterials()
	{
		static const TArray<FName> Names = { TEXT("CloudMaterial"), TEXT("SphereMaterial"), TEXT("CurrentDynamicParam") };
		return Names;
	}
	constexpr uint32 MaxRawBytes = 4 * 1024 * 1024;
	constexpr uint32 MaxCompressedBytes = 48 * 1024;
	constexpr double BoundaryTolerance = 1.e-6;
	const TCHAR* WeatherStructPath = TEXT("/Game/StylizedWeather/Blueprint/Struct_Weather.Struct_Weather");
	const TCHAR* OverrideStructPath = TEXT("/Game/StylizedWeather/Blueprint/Struct_ScalarParamOverride.Struct_ScalarParamOverride");
	const TCHAR* WeatherEnumPath = TEXT("/Game/StylizedWeather/Blueprint/Enum_Weaher.Enum_Weaher");
	const FName WeatherNameField(TEXT("WeatherName_26_F72244384181F932930C06A074C9BC4D"));
	const FName WeightField(TEXT("Weight_25_E779B5B244389F928CC6F3AD130B9EEA"));
	const FName MaterialField(TEXT("MaterialInstance_37_6AE97BF348D227751A816595DBDC2670"));
	const FName SunColorField(TEXT("SunColor_19_1A68A0664FA7D88543A94EB791BF27BC"));
	const FName SkyColorField(TEXT("SkyLightColor_22_4895B0E140DA8DDA0FF0468DEA02380A"));
	const FName FogColorField(TEXT("FogColor_42_165ADA774FC44E89B22F6D9220A38315"));
	const FName OverallColorField(TEXT("OverallColor_53_9180AB814770FBFCB26B6EBE53254EEB"));
	const FName OverridesField(TEXT("ScalarParamOverride_35_0FB9EF2C4407839DBD7EFF86050D4F3B"));
	const FName VisibleField(TEXT("SunVisible_49_EF4F0C624EE4F7C55381E2920ADD7A12"));
	const FName ParamNameField(TEXT("ParamName_37_F72244384181F932930C06A074C9BC4D"));
	const FName ParamMinField(TEXT("ParamMin_41_DF87CBF84BAAF98D7BB3F0AB42B2E927"));
	const FName ParamMaxField(TEXT("ParamMax_42_CBFAFB6247634FD79F316095342ECA41"));
	const FName CurveField(TEXT("InterpolateCurve_34_4895B0E140DA8DDA0FF0468DEA02380A"));

	uint64 SegmentKey(const FSWWeatherPlaybackSegment& Segment)
	{
		return (static_cast<uint64>(Segment.Epoch) << 32) | Segment.Sequence;
	}
	bool FiniteColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
	}
	bool BoundedString(const FString& Value, int32 Limit)
	{
		for (TCHAR C : Value) if (!C) return false;
		return Value.Len() <= Limit && FTCHARToUTF8(*Value).Length() <= Limit;
	}
	bool ParameterName(const FString& Name)
	{
		if (Name.IsEmpty() || !BoundedString(Name, 256)) return false;
		for (TCHAR C : Name) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_')) return false;
		return true;
	}
	const TCHAR* AssetPrefix(uint8 Kind)
	{
		switch (Kind)
		{
		case 0: return TEXT("/Game/StylizedWeather/Material/Sky/");
		case 1: return TEXT("/Game/StylizedWeather/Curve/Color/");
		case 2: return TEXT("/Game/StylizedWeather/Curve/Float/");
		case 3: return TEXT("/Game/StylizedWeather/Texture/");
		default: return TEXT("");
		}
	}
	UClass* AssetClass(uint8 Kind)
	{
		switch (Kind)
		{
		case 0: return UMaterialInstanceConstant::StaticClass();
		case 1: return UCurveLinearColor::StaticClass();
		case 2: return UCurveFloat::StaticClass();
		case 3: return UTexture2D::StaticClass();
		default: return nullptr;
		}
	}
	bool ValidAssetPath(const FString& Path, uint8 Kind)
	{
		if (Kind > 3 || !BoundedString(Path, 512) || !Path.StartsWith(AssetPrefix(Kind), ESearchCase::CaseSensitive)
			|| Path.Contains(TEXT("..")) || Path.Contains(TEXT(":")) || Path.Contains(TEXT("\\"))) return false;
		for (TCHAR C : Path) if (FChar::IsWhitespace(C) || C == '\'' || C == '"') return false;
		int32 Dot = INDEX_NONE, Slash = INDEX_NONE;
		if (!Path.FindLastChar('.', Dot) || !Path.FindLastChar('/', Slash) || Dot <= Slash + 1) return false;
		return Path.Find(TEXT(".")) == Dot && Path.Mid(Slash + 1, Dot - Slash - 1) == Path.Mid(Dot + 1);
	}
	bool ValidAsset(UObject* Object, const FString& Path, uint8 Kind)
	{
		return Object && ValidAssetPath(Path, Kind) && Object->GetPathName() == Path && Object->GetClass() == AssetClass(Kind)
			&& !Object->HasAnyFlags(RF_Transient) && !Object->GetTypedOuter<AActor>() && !Object->GetTypedOuter<UWorld>();
	}
	bool ReferencePolicy(FName Root, const FString& Member, ESWWeatherAssetKind& Kind, bool& bNullable)
	{
		bNullable = false;
		if (Member.IsEmpty())
		{
			if (Root == TEXT("CurrentParam") || Root == TEXT("NextParam")) Kind = ESWWeatherAssetKind::SkyMaterial;
			else if (Root.ToString().StartsWith(TEXT("Current")) || Root.ToString().StartsWith(TEXT("Next"))) Kind = ESWWeatherAssetKind::ColorCurve;
			else return false;
			return Root != TEXT("NextWeather") && Root != TEXT("NextWeather Buf");
		}
		if (Root != TEXT("NextWeather") && Root != TEXT("NextWeather Buf")) return false;
		bNullable = Root == TEXT("NextWeather Buf");
		if (Member == MaterialField.ToString()) { Kind = ESWWeatherAssetKind::SkyMaterial; bNullable = true; return true; }
		for (FName Name : {SunColorField, SkyColorField, FogColorField, OverallColorField})
			if (Member == Name.ToString()) { Kind = ESWWeatherAssetKind::ColorCurve; return true; }
		const FString Prefix = OverridesField.ToString() + TEXT("[");
		const FString Suffix = TEXT("].") + CurveField.ToString();
		if (!Member.StartsWith(Prefix) || !Member.EndsWith(Suffix)) return false;
		const FString IndexText = Member.Mid(Prefix.Len(), Member.Len() - Prefix.Len() - Suffix.Len());
		if (IndexText.IsEmpty() || IndexText.Len() > 3) return false;
		for (TCHAR C : IndexText) if (C < '0' || C > '9') return false;
		const int32 Index = FCString::Atoi(*IndexText);
		if (Index >= 256 || FString::FromInt(Index) != IndexText) return false;
		Kind = ESWWeatherAssetKind::ScalarCurve;
		return true;
	}
	void Canonicalize(FSWWeatherPlaybackSegment& Segment)
	{
		Segment.Properties.Sort([](const auto& A, const auto& B) { return A.Name.LexicalLess(B.Name); });
		for (auto& Property : Segment.Properties) Property.References.Sort([](const auto& A, const auto& B) { return A.MemberPath < B.MemberPath; });
		Segment.Materials.Sort([](const auto& A, const auto& B) { return A.PropertyName.LexicalLess(B.PropertyName); });
		for (auto& Material : Segment.Materials)
		{
			Material.Scalars.Sort([](const auto& A, const auto& B) { return A.Name.LexicalLess(B.Name); });
			Material.Vectors.Sort([](const auto& A, const auto& B) { return A.Name.LexicalLess(B.Name); });
			Material.Textures.Sort([](const auto& A, const auto& B) { return A.Name.LexicalLess(B.Name); });
		}
	}
	bool TokenManifest(const FSWWeatherPropertyValue& P);
	bool BoundedPropertyText(const FString& Text);
	bool ValidateSegment(const FSWWeatherPlaybackSegment& S)
	{
		if (!S.Epoch || !S.Sequence || S.Hour < 0 || S.Hour > 23 || !FMath::IsFinite(S.StartServerTime)
			|| !FMath::IsFinite(S.SecondsPerHour) || S.SecondsPerHour < 0. || !FMath::IsFinite(S.StartServerTime + S.SecondsPerHour)
			|| !FMath::IsFinite(S.FrozenAlpha) || S.FrozenAlpha < 0. || S.FrozenAlpha > 1.
			|| !FMath::IsFinite(S.SunVisibilityFrom) || S.SunVisibilityFrom < 0.f || S.SunVisibilityFrom > 1.f
			|| !FMath::IsFinite(S.SunVisibilityTo) || S.SunVisibilityTo < 0.f || S.SunVisibilityTo > 1.f
			|| S.Properties.Num() != 14 || S.Materials.Num() != 3) return false;
		TSet<FName> Names;
		for (const auto& P : S.Properties)
		{
			if (!WeatherProperties().Contains(P.Name) || Names.Contains(P.Name) || !BoundedString(P.Value, 65536) || P.References.Num() > 2048) return false;
			Names.Add(P.Name);
			TSet<FString> Members;
			for (const auto& R : P.References)
			{
				ESWWeatherAssetKind Kind; bool bNullable;
				if (!BoundedString(R.MemberPath, 1024) || Members.Contains(R.MemberPath) || !ReferencePolicy(P.Name, R.MemberPath, Kind, bNullable)
					|| R.Kind != Kind || (R.bIsNull ? (!bNullable || !R.ObjectPath.IsEmpty()) : !ValidAssetPath(R.ObjectPath, static_cast<uint8>(Kind)))) return false;
				Members.Add(R.MemberPath);
			}
			if (P.Name == TEXT("InitWeather") || P.Name == TEXT("SunVisible")) { if (P.References.Num()) return false; }
			else if (P.Name == TEXT("NextWeather") || P.Name == TEXT("NextWeather Buf"))
			{
				for (FName N : {MaterialField, SunColorField, SkyColorField, FogColorField, OverallColorField}) if (!Members.Contains(N.ToString())) return false;
			}
			else if (P.References.Num() != 1 || !Members.Contains(TEXT(""))) return false;
			if (!TokenManifest(P) || !BoundedPropertyText(P.Value)) return false;
		}
		Names.Reset();
		for (const auto& M : S.Materials)
		{
			if (!WeatherMaterials().Contains(M.PropertyName) || Names.Contains(M.PropertyName) || M.Scalars.Num() > 256 || M.Vectors.Num() > 256 || M.Textures.Num() > 256) return false;
			Names.Add(M.PropertyName);
			TSet<FName> Parameters;
			for (const auto& V : M.Scalars) { if (!BoundedString(V.Name.ToString(), 256) || Parameters.Contains(V.Name) || !FMath::IsFinite(V.Value)) return false; Parameters.Add(V.Name); }
			Parameters.Reset();
			for (const auto& V : M.Vectors) { if (!BoundedString(V.Name.ToString(), 256) || Parameters.Contains(V.Name) || !FiniteColor(V.Value)) return false; Parameters.Add(V.Name); }
			Parameters.Reset();
			for (const auto& T : M.Textures)
			{
				const bool bRequired = T.Name == TEXT("SkyColor_Texture") || (M.PropertyName != TEXT("CurrentDynamicParam") && T.Name == TEXT("BlendBroker0"));
				if (!BoundedString(T.Name.ToString(), 256) || Parameters.Contains(T.Name)
					|| (T.bIsNull ? (bRequired || !T.ObjectPath.IsEmpty()) : !ValidAssetPath(T.ObjectPath, 3))) return false;
				Parameters.Add(T.Name);
			}
			if (!Parameters.Contains(TEXT("SkyColor_Texture")) || (M.PropertyName != TEXT("CurrentDynamicParam") && !Parameters.Contains(TEXT("BlendBroker0")))) return false;
		}
		return true;
	}
	bool ValidateWindow(const FSWWeatherPlaybackWindow& W)
	{
		if (W.Generation < 0 || !W.Epoch || W.Segments.IsEmpty() || W.Segments.Num() > 256 || !FMath::IsFinite(W.PublishedServerTime)
			|| !FMath::IsFinite(W.EpochPlaybackStartServerTime) || W.PlaybackDelaySeconds != 2.) return false;
		const FSWWeatherPlaybackSegment* Previous = nullptr;
		for (const auto& S : W.Segments)
		{
			if (!ValidateSegment(S) || S.Epoch != W.Epoch) return false;
			if (S.SecondsPerHour == 0. && W.Segments.Num() != 1) return false;
			if (Previous && (S.Sequence != Previous->Sequence + 1 || S.Hour != (Previous->Hour + 1) % 24
				|| S.SecondsPerHour != Previous->SecondsPerHour || FMath::Abs(S.StartServerTime - Previous->StartServerTime - Previous->SecondsPerHour) > BoundaryTolerance)) return false;
			Previous = &S;
		}
		return true;
	}

	// Explicit little-endian value codec. Counts and byte availability are checked before allocation.
	struct FWeatherBytes
	{
		TArray<uint8> Bytes;
		int32 Offset = 0;
		bool bReading = false;
		bool bValid = true;
		template<typename T> void Number(T& Value)
		{
			if (!bValid) return;
			if (!bReading && Bytes.Num() + sizeof(T) > MaxRawBytes) { bValid = false; return; }
			if (bReading && Bytes.Num() - Offset < static_cast<int32>(sizeof(T))) { bValid = false; return; }
			uint8 Bits[sizeof(T)];
			if (!bReading) FMemory::Memcpy(Bits, &Value, sizeof(T));
			for (uint32 I = 0; I < sizeof(T); ++I)
			{
				if (bReading) Bits[I] = Bytes[Offset++]; else Bytes.Add(Bits[I]);
			}
			if (bReading) FMemory::Memcpy(&Value, Bits, sizeof(T));
			if (Bytes.Num() > MaxRawBytes) bValid = false;
		}
		void String(FString& Value, uint32 Limit)
		{
			FTCHARToUTF8 Encoded(*Value);
			uint32 Count = bReading ? 0 : static_cast<uint32>(Encoded.Length());
			Number(Count);
			if (!bValid || Count > Limit || (bReading && Count > static_cast<uint32>(Bytes.Num() - Offset))) { bValid = false; return; }
			if (!bReading)
			{
				if (static_cast<uint32>(Bytes.Num()) + Count > MaxRawBytes) { bValid = false; return; }
				Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Count); return;
			}
			// Reject overlong sequences, surrogates, embedded NUL and out-of-range Unicode.
			for (uint32 I = 0; I < Count;)
			{
				uint32 C = Bytes[Offset + I++], N = 0, Minimum = 0;
				if (C == 0) { bValid = false; return; }
				if (C < 0x80) continue;
				if (C >= 0xC2 && C <= 0xDF) { N = 1; Minimum = 0x80; C &= 0x1F; }
				else if (C >= 0xE0 && C <= 0xEF) { N = 2; Minimum = 0x800; C &= 0x0F; }
				else if (C >= 0xF0 && C <= 0xF4) { N = 3; Minimum = 0x10000; C &= 7; }
				else { bValid = false; return; }
				if (I + N > Count) { bValid = false; return; }
				while (N--) { const uint8 B = Bytes[Offset + I++]; if ((B & 0xC0) != 0x80) { bValid = false; return; } C = (C << 6) | (B & 0x3F); }
				if (C < Minimum || C > 0x10FFFF || (C >= 0xD800 && C <= 0xDFFF)) { bValid = false; return; }
			}
			FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Count);
			Value = FString(Decoded.Length(), Decoded.Get()); Offset += Count;
		}
		void Name(FName& Value) { FString Text = bReading ? FString() : Value.ToString(); String(Text, 256); if (bReading && bValid) Value = FName(*Text); }
		void Bool(bool& Value) { uint8 B = Value ? 1 : 0; Number(B); if (B > 1) bValid = false; Value = B != 0; }
		template<typename T, typename F> void Array(TArray<T>& Values, uint32 Limit, F Element)
		{
			uint32 Count = bReading ? 0 : static_cast<uint32>(Values.Num()); Number(Count);
			if (!bValid || Count > Limit || (bReading && Count > static_cast<uint32>((Bytes.Num() - Offset) / 4))) { bValid = false; return; }
			if (bReading) Values.SetNum(Count);
			for (T& V : Values) { if (!bValid) break; Element(V); }
		}
		void Segment(FSWWeatherPlaybackSegment& S)
		{
			Number(S.Epoch); Number(S.Sequence); Number(S.Hour); Number(S.StartServerTime); Number(S.SecondsPerHour); Number(S.FrozenAlpha); Number(S.SunVisibilityFrom); Number(S.SunVisibilityTo);
			Array(S.Properties, 14, [this](FSWWeatherPropertyValue& P)
			{
				Name(P.Name); String(P.Value, 65536);
				Array(P.References, 2048, [this](FSWWeatherAssetReference& R) { String(R.MemberPath, 1024); String(R.ObjectPath, 512); uint8 Kind = static_cast<uint8>(R.Kind); Number(Kind); R.Kind = static_cast<ESWWeatherAssetKind>(Kind); Bool(R.bIsNull); });
			});
			Array(S.Materials, 3, [this](FSWWeatherMaterialState& M)
			{
				Name(M.PropertyName);
				Array(M.Scalars, 256, [this](FSWWeatherScalarValue& V) { Name(V.Name); Number(V.Value); });
				Array(M.Vectors, 256, [this](FSWWeatherVectorValue& V) { Name(V.Name); Number(V.Value.R); Number(V.Value.G); Number(V.Value.B); Number(V.Value.A); });
				Array(M.Textures, 256, [this](FSWWeatherTextureValue& V) { Name(V.Name); String(V.ObjectPath, 512); Bool(V.bIsNull); });
			});
		}
		void Window(FSWWeatherPlaybackWindow& W)
		{
			Number(W.Generation); Number(W.Epoch); Number(W.PublishedServerTime); Number(W.EpochPlaybackStartServerTime); Number(W.PlaybackDelaySeconds);
			Array(W.Segments, 256, [this](auto& S) { Segment(S); });
		}
	};

	bool SameSegment(const FSWWeatherPlaybackSegment& A, const FSWWeatherPlaybackSegment& B)
	{
		FSWWeatherPlaybackSegment Adjusted = B;
		if (A.Epoch != B.Epoch || A.Sequence != B.Sequence || A.Hour != B.Hour
			|| FMath::Abs(A.StartServerTime - B.StartServerTime) > 1.e-6 || FMath::Abs(A.SecondsPerHour - B.SecondsPerHour) > 1.e-6
			|| FMath::Abs(A.FrozenAlpha - B.FrozenAlpha) > 1.e-6 || FMath::Abs(A.SunVisibilityFrom - B.SunVisibilityFrom) > 1.e-6
			|| FMath::Abs(A.SunVisibilityTo - B.SunVisibilityTo) > 1.e-6 || A.Materials.Num() != B.Materials.Num()) return false;
		Adjusted.StartServerTime = A.StartServerTime; Adjusted.SecondsPerHour = A.SecondsPerHour; Adjusted.FrozenAlpha = A.FrozenAlpha;
		Adjusted.SunVisibilityFrom = A.SunVisibilityFrom; Adjusted.SunVisibilityTo = A.SunVisibilityTo;
		for (int32 I = 0; I < A.Materials.Num(); ++I)
		{
			const auto& X = A.Materials[I]; auto& Y = Adjusted.Materials[I];
			if (X.Scalars.Num() != Y.Scalars.Num() || X.Vectors.Num() != Y.Vectors.Num()) return false;
			for (int32 J = 0; J < X.Scalars.Num(); ++J) { if (FMath::Abs(X.Scalars[J].Value - Y.Scalars[J].Value) > 1.e-6) return false; Y.Scalars[J].Value = X.Scalars[J].Value; }
			for (int32 J = 0; J < X.Vectors.Num(); ++J) { if (!X.Vectors[J].Value.Equals(Y.Vectors[J].Value, 1.e-6f)) return false; Y.Vectors[J].Value = X.Vectors[J].Value; }
		}
		FSWWeatherPlaybackSegment Copy = A;
		FWeatherBytes Left, Right; Left.Segment(Copy); Right.Segment(Adjusted);
		return Left.bValid && Right.bValid && Left.Bytes == Right.Bytes;
	}
	bool TokenManifest(const FSWWeatherPropertyValue& P)
	{
		TArray<FString> Expected, Actual;
		for (const auto& R : P.References) if (!R.bIsNull) Expected.Add(AssetClass(static_cast<uint8>(R.Kind))->GetPathName() + TEXT("'") + R.ObjectPath + TEXT("'"));
		const FString& Text = P.Value;
		for (int32 I = 0; I < Text.Len(); ++I)
		{
			if (Text[I] == '\'') return false;
			if (Text[I] != '/') continue;
			if (!Text.Mid(I).StartsWith(TEXT("/Script/Engine."))) return false;
			const int32 Quote = Text.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
			const int32 End = Quote == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Quote + 1);
			if (End == INDEX_NONE) return false;
			Actual.Add(Text.Mid(I, End - I + 1)); I = End;
		}
		Expected.Sort(); Actual.Sort(); return Expected == Actual;
	}

	struct FPropertyScratch
	{
		FProperty* Property;
		void* Value;
		explicit FPropertyScratch(FProperty* InProperty) : Property(InProperty), Value(FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment())) { Property->InitializeValue(Value); }
		~FPropertyScratch() { Property->DestroyValue(Value); FMemory::Free(Value); }
	};
	// Saves complete reflected values, including struct arrays, rather than export/import text.
	struct FWeatherContext
	{
		UObject* Owner;
		float* Visibility;
		float SavedVisibility;
		TArray<TUniquePtr<FPropertyScratch>> Values;
		explicit FWeatherContext(UObject* InOwner, float* InVisibility = nullptr) : Owner(InOwner), Visibility(InVisibility), SavedVisibility(InVisibility ? *InVisibility : 0.f)
		{
			TArray<FName> Names = WeatherProperties(); Names.Append(WeatherMaterials());
			Names.Append({TEXT("CurrentHour"), TEXT("Current Minute")});
			for (FName Name : Names)
			{
				FProperty* P = Owner->GetClass()->FindPropertyByName(Name);
				if (P) { auto Saved = MakeUnique<FPropertyScratch>(P); P->CopyCompleteValue(Saved->Value, P->ContainerPtrToValuePtr<void>(Owner)); Values.Add(MoveTemp(Saved)); }
			}
		}
		~FWeatherContext()
		{
			for (const auto& Saved : Values) Saved->Property->CopyCompleteValue(Saved->Property->ContainerPtrToValuePtr<void>(Owner), Saved->Value);
			if (Visibility) *Visibility = SavedVisibility;
		}
	};
	template<typename T> T* ResolveComponent(UObject* Object)
	{
		if (T* Component = Cast<T>(Object)) return Component;
		if (AActor* Actor = Cast<AActor>(Object)) return Actor->FindComponentByClass<T>();
		return nullptr;
	}
	bool ValidateLayout(FProperty* P)
	{
		if (!P || P->ArrayDim != 1) return false;
		if ((P->GetFName() == TEXT("SunVisible") || P->GetFName() == VisibleField) && !CastField<FBoolProperty>(P)) return false;
		if ((P->GetFName() == TEXT("InitWeather") || P->GetFName() == WeatherNameField) && !CastField<FByteProperty>(P) && !CastField<FEnumProperty>(P)) return false;
		if (P->GetFName() == WeightField && !CastField<FNumericProperty>(P)) return false;
		if (P->GetFName() == ParamNameField && !CastField<FNameProperty>(P)) return false;
		if (FStructProperty* S = CastField<FStructProperty>(P))
		{
			TArray<FName> Expected;
			if (S->Struct->GetPathName() == WeatherStructPath) Expected = {WeatherNameField, WeightField, MaterialField, SunColorField, SkyColorField, FogColorField, OverallColorField, OverridesField, VisibleField};
			else if (S->Struct->GetPathName() == OverrideStructPath) Expected = {ParamNameField, ParamMinField, ParamMaxField, CurveField};
			else return false;
			int32 Count = 0;
			for (TFieldIterator<FProperty> It(S->Struct); It; ++It) { if (!Expected.Contains(It->GetFName()) || !ValidateLayout(*It)) return false; ++Count; }
			return Count == Expected.Num();
		}
		if (FArrayProperty* A = CastField<FArrayProperty>(P)) return P->GetFName() == OverridesField && CastField<FStructProperty>(A->Inner) && ValidateLayout(A->Inner);
		if (FObjectProperty* O = CastField<FObjectProperty>(P)) return O->PropertyClass == UMaterialInterface::StaticClass() || O->PropertyClass == UMaterialInstance::StaticClass() || O->PropertyClass == UMaterialInstanceConstant::StaticClass()
			|| O->PropertyClass == UCurveLinearColor::StaticClass() || O->PropertyClass == UCurveFloat::StaticClass();
		if (FByteProperty* B = CastField<FByteProperty>(P)) return B->Enum && B->Enum->GetPathName() == WeatherEnumPath;
		if (FEnumProperty* E = CastField<FEnumProperty>(P)) return E->GetEnum() && E->GetEnum()->GetPathName() == WeatherEnumPath;
		return CastField<FBoolProperty>(P) || CastField<FNumericProperty>(P) || CastField<FNameProperty>(P);
	}
	bool WalkValue(FProperty* P, const void* Address, FName Root, const FString& Path, TArray<FSWWeatherAssetReference>& References, FString* Failure = nullptr)
	{
		auto Reject = [&](const FString& Reason)
		{
			if (Failure) *Failure = Root.ToString() + (Path.IsEmpty() ? TEXT("") : TEXT(".") + Path) + TEXT(":") + Reason;
			return false;
		};
		if (!ValidateLayout(P)) return Reject(TEXT("Layout"));
		if (const FObjectPropertyBase* O = CastField<FObjectPropertyBase>(P))
		{
			ESWWeatherAssetKind Kind; bool bNullable;
			if (!ReferencePolicy(Root, Path, Kind, bNullable)) return Reject(TEXT("ReferencePolicy"));
			UObject* Object = O->GetObjectPropertyValue(Address);
			if (!Object && !bNullable) return Reject(TEXT("RequiredReferenceNull"));
			FSWWeatherAssetReference R; R.MemberPath = Path; R.Kind = Kind; R.bIsNull = !Object;
			if (Object) { R.ObjectPath = Object->GetPathName(); if (!ValidAsset(Object, R.ObjectPath, static_cast<uint8>(Kind))) return Reject(TEXT("InvalidAsset=") + R.ObjectPath); }
			References.Add(MoveTemp(R)); return References.Num() <= 2048 || Reject(TEXT("ReferenceCount"));
		}
		if (const FStructProperty* S = CastField<FStructProperty>(P))
		{
			for (TFieldIterator<FProperty> It(S->Struct); It; ++It)
			{
				const FString Member = Path.IsEmpty() ? It->GetName() : Path + TEXT(".") + It->GetName();
				if (!WalkValue(*It, It->ContainerPtrToValuePtr<void>(Address), Root, Member, References, Failure)) return false;
			}
			return true;
		}
		if (const FArrayProperty* A = CastField<FArrayProperty>(P))
		{
			FScriptArrayHelper Array(A, Address);
			if (Array.Num() > 256) return Reject(TEXT("ArrayCount"));
			for (int32 I = 0; I < Array.Num(); ++I) if (!WalkValue(A->Inner, Array.GetRawPtr(I), Root, FString::Printf(TEXT("%s[%d]"), *Path, I), References, Failure)) return false;
			return true;
		}
		if (const FNameProperty* N = CastField<FNameProperty>(P)) return (P->GetFName() == ParamNameField && ParameterName(N->GetPropertyValue(Address).ToString())) || Reject(TEXT("ParameterName=") + N->GetPropertyValue(Address).ToString());
		UEnum* Enum = nullptr; int64 EnumValue = 0;
		if (const FByteProperty* B = CastField<FByteProperty>(P)) { Enum = B->Enum; EnumValue = B->GetPropertyValue(Address); }
		if (const FEnumProperty* E = CastField<FEnumProperty>(P)) { Enum = E->GetEnum(); EnumValue = E->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address); }
		if (Enum) return (Enum->IsValidEnumValue(EnumValue) && Enum->GetIndexByValue(EnumValue) >= 0 && Enum->GetIndexByValue(EnumValue) < Enum->NumEnums() - 1) || Reject(FString::Printf(TEXT("EnumValue=%lld"), EnumValue));
		if (const FNumericProperty* N = CastField<FNumericProperty>(P))
		{
			const double Value = N->IsFloatingPoint() ? N->GetFloatingPointPropertyValue(Address) : static_cast<double>(N->GetSignedIntPropertyValue(Address));
			return (FMath::IsFinite(Value) && (P->GetFName() != WeightField || Value >= 0.)) || Reject(FString::Printf(TEXT("NumericValue=%.9g"), Value));
		}
		return CastField<FBoolProperty>(P) != nullptr || Reject(TEXT("UnsupportedType"));
	}
	bool SameReferences(TArray<FSWWeatherAssetReference> A, const TArray<FSWWeatherAssetReference>& B)
	{
		A.Sort([](const auto& X, const auto& Y) { return X.MemberPath < Y.MemberPath; });
		if (A.Num() != B.Num()) return false;
		for (int32 I = 0; I < A.Num(); ++I) if (A[I].MemberPath != B[I].MemberPath || A[I].ObjectPath != B[I].ObjectPath || A[I].Kind != B[I].Kind || A[I].bIsNull != B[I].bIsNull) return false;
		return true;
	}
	bool BoundedPropertyText(const FString& Text)
	{
		int32 Depth = 0; int32 Counts[16] = {}; bool bQuoted = false;
		for (int32 I = 0; I < Text.Len(); ++I)
		{
			const TCHAR C = Text[I];
			if (C == '"' && (I == 0 || Text[I - 1] != '\\')) bQuoted = !bQuoted;
			if (bQuoted) continue;
			if (C == '(') { if (++Counts[Depth] > 256 || ++Depth >= 16) return false; Counts[Depth] = 0; }
			else if (C == ')') { if (--Depth < 0) return false; }
		}
		return Depth == 0 && !bQuoted;
	}
	bool CaptureMaterial(UMaterialInstanceDynamic* MID, FName Name, FSWWeatherMaterialState& M)
	{
		if (!MID || MID->ScalarParameterValues.Num() > 256 || MID->VectorParameterValues.Num() > 256 || MID->TextureParameterValues.Num() > 256
			|| MID->FontParameterValues.Num() != 0) return false;
		M = FSWWeatherMaterialState(); M.PropertyName = Name;
		for (const auto& V : MID->ScalarParameterValues)
		{
			if (V.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter || !FMath::IsFinite(V.ParameterValue)) return false;
			FSWWeatherScalarValue& E = M.Scalars.AddDefaulted_GetRef(); E.Name = V.ParameterInfo.Name; E.Value = V.ParameterValue;
		}
		for (const auto& V : MID->VectorParameterValues)
		{
			if (V.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter || !FiniteColor(V.ParameterValue)) return false;
			FSWWeatherVectorValue& E = M.Vectors.AddDefaulted_GetRef(); E.Name = V.ParameterInfo.Name; E.Value = V.ParameterValue;
		}
		for (const auto& V : MID->TextureParameterValues)
		{
			if (V.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter) return false;
			FSWWeatherTextureValue& E = M.Textures.AddDefaulted_GetRef(); E.Name = V.ParameterInfo.Name; E.bIsNull = !V.ParameterValue;
			if (V.ParameterValue) { E.ObjectPath = V.ParameterValue->GetPathName(); if (!ValidAsset(V.ParameterValue, E.ObjectPath, 3)) return false; }
		}
		return true;
	}
}

bool FSWWeatherPlaybackWindow::PreparePayload()
{
	for (auto& S : Segments) Canonicalize(S);
	if (!ValidateWindow(*this)) return false;
	FWeatherBytes Writer; Writer.Window(*this);
	if (!Writer.bValid || Writer.Bytes.Num() > MaxRawBytes) return false;
	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, Writer.Bytes.Num());
	TArray<uint8> Payload; Payload.SetNumUninitialized(CompressedSize);
	if (!FCompression::CompressMemory(NAME_Zlib, Payload.GetData(), CompressedSize, Writer.Bytes.GetData(), Writer.Bytes.Num()) || CompressedSize > MaxCompressedBytes) return false;
	Payload.SetNum(CompressedSize); RawPayloadBytes = Writer.Bytes.Num(); CompressedPayload = MoveTemp(Payload);
	return true;
}

bool FSWWeatherPlaybackWindow::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	uint8 Version = 2;
	uint32 Raw = Ar.IsSaving() ? RawPayloadBytes : 0, Compressed = Ar.IsSaving() ? CompressedPayload.Num() : 0;
	auto HeaderNumber = [&Ar](auto& Value)
	{
		uint8 Bytes[sizeof(Value)] = {};
		if (Ar.IsSaving()) FMemory::Memcpy(Bytes, &Value, sizeof(Value));
		Ar.Serialize(Bytes, sizeof(Value));
		if (Ar.IsLoading()) FMemory::Memcpy(&Value, Bytes, sizeof(Value));
	};
	HeaderNumber(Version); HeaderNumber(Raw); HeaderNumber(Compressed);
	bOutSuccess = false;
	if (Ar.IsError() || Version != 2 || Raw > MaxRawBytes || Compressed > MaxCompressedBytes || ((Raw == 0) != (Compressed == 0))) { Ar.SetError(); return true; }
	if (!Raw) { if (Ar.IsLoading()) *this = FSWWeatherPlaybackWindow(); bOutSuccess = true; return true; }
	if (Ar.IsSaving()) { Ar.Serialize(CompressedPayload.GetData(), Compressed); bOutSuccess = !Ar.IsError(); return true; }
	TArray<uint8> Payload; Payload.SetNumUninitialized(Compressed); Ar.Serialize(Payload.GetData(), Compressed);
	FWeatherBytes Reader; Reader.bReading = true;
	if (!Ar.IsError())
	{
		Reader.Bytes.SetNumUninitialized(Raw);
		if (FCompression::UncompressMemory(NAME_Zlib, Reader.Bytes.GetData(), Raw, Payload.GetData(), Compressed))
		{
			FSWWeatherPlaybackWindow Scratch; Reader.Window(Scratch);
			if (Reader.bValid && Reader.Offset == Reader.Bytes.Num() && ValidateWindow(Scratch))
			{
				for (auto& S : Scratch.Segments) Canonicalize(S);
				Scratch.RawPayloadBytes = Raw; Scratch.CompressedPayload = MoveTemp(Payload); *this = MoveTemp(Scratch); bOutSuccess = true;
			}
		}
	}
	if (!bOutSuccess)
	{
		static bool bReportedInvalidWire = false;
		if (!bReportedInvalidWire)
		{
			// UE_LOG(LogSWWeatherDiagnostics, Warning, TEXT("ReceivedWindowInvalid"));
			bReportedInvalidWire = true;
		}
	}
	return true;
}

ASWNetworkWeatherActor::ASWNetworkWeatherActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	SkyParameters = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/StylizedWeather/Material/MPC_Sky.MPC_Sky"));
}

void ASWNetworkWeatherActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWNetworkWeatherActor, PlaybackWindow);
	DOREPLIFETIME(ASWNetworkWeatherActor, WindState);
}

bool ASWNetworkWeatherActor::ValidateAdapter() const
{
	for (FName Name : WeatherProperties()) if (!ValidateLayout(GetClass()->FindPropertyByName(Name))) return false;
	for (FName Name : WeatherMaterials()) if (!FindFProperty<FObjectPropertyBase>(GetClass(), Name)) return false;
	for (FName Name : {FName(TEXT("CurrentHour")), FName(TEXT("Current Minute"))}) if (!FindFProperty<FNumericProperty>(GetClass(), Name)) return false;
	for (FName Name : { FName(TEXT("CreateDynamicMat")), FName(TEXT("InitializeSetting")), FName(TEXT("WeatherRaffleSetting")),
		FName(TEXT("SetNextWeather")), FName(TEXT("ChangeMatParam")), FName(TEXT("Change Param")), FName(TEXT("WeatherEventCall Setup")), FName(TEXT("WeatherEventCall")) })
		if (!FindFunction(Name)) return false;
	return SkyParameters != nullptr;
}

bool ASWNetworkWeatherActor::Invoke(FName Name, const TMap<FName, double>& Arguments)
{
	UFunction* Function = FindFunction(Name);
	if (!Function) return false;
	FStructOnScope Parameters(Function);
	for (const TPair<FName, double>& Argument : Arguments)
	{
		FNumericProperty* Property = FindFProperty<FNumericProperty>(Function, Argument.Key);
		if (!Property) return false;
		void* Address = Property->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory());
		if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address, Argument.Value);
		else Property->SetIntPropertyValue(Address, static_cast<int64>(Argument.Value));
	}
	ProcessEvent(Function, Parameters.GetStructMemory());
	return true;
}

double ASWNetworkWeatherActor::ReadNumber(FName Name) const
{
	if (const FNumericProperty* Property = FindFProperty<FNumericProperty>(GetClass(), Name))
	{
		const void* Address = Property->ContainerPtrToValuePtr<void>(this);
		return Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Address) : Property->GetSignedIntPropertyValue(Address);
	}
	if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(GetClass(), Name)) return Property->GetPropertyValue_InContainer(this) ? 1. : 0.;
	return 0.;
}

void ASWNetworkWeatherActor::WriteNumber(FName Name, double Value)
{
	if (FNumericProperty* Property = FindFProperty<FNumericProperty>(GetClass(), Name))
	{
		void* Address = Property->ContainerPtrToValuePtr<void>(this);
		if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address, Value);
		else Property->SetIntPropertyValue(Address, static_cast<int64>(Value));
	}
	else if (FBoolProperty* Bool = FindFProperty<FBoolProperty>(GetClass(), Name)) Bool->SetPropertyValue_InContainer(this, Value != 0.);
}

UObject* ASWNetworkWeatherActor::ReadObject(FName Name) const
{
	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), Name);
	return Property ? Property->GetObjectPropertyValue_InContainer(this) : nullptr;
}

double ASWNetworkWeatherActor::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (HasAuthority() && World ? World->GetTimeSeconds() : 0.);
}

double ASWNetworkWeatherActor::ServerWeatherRandomFloat(double Min, double Max) const
{
	return GetWorld() && GetWorld()->IsGameWorld() && HasAuthority() ? FMath::FRandRange(Min, Max) : Min;
}

int32 ASWNetworkWeatherActor::ServerWeatherRandomInteger(int32 Min, int32 Max) const
{
	return GetWorld() && GetWorld()->IsGameWorld() && HasAuthority() ? FMath::RandRange(Min, Max) : Min;
}

void ASWNetworkWeatherActor::SetNetworkSunVisible()
{
	WriteNumber(TEXT("SunVisible"), ReadNumber(TEXT("SunVisible")) == 0. ? 1. : 0.);
}

FVector ASWNetworkWeatherActor::ResolveWindOffset(double ServerTime) const
{
	const double Age = FMath::Max(0., ServerTime - WindState.StartServerTime);
	const double Duration = FMath::Max(0.001, WindState.Duration);
	const double BlendTime = FMath::Min(Age, Duration);
	const FVector Integral = WindState.From * BlendTime + (WindState.To - WindState.From) * (0.5 * BlendTime * BlendTime / Duration)
		+ WindState.To * FMath::Max(0., Age - Duration);
	return WindState.OffsetAtStart + Integral * WindState.Speed;
}

void ASWNetworkWeatherActor::AdvanceWind(double Now)
{
	if (!HasAuthority() || ReadNumber(TEXT("WindChange")) == 0. || Now < NextWindServerTime) return;
	const FVector Offset = ResolveWindOffset(Now);
	const double Alpha = FMath::Clamp((Now - WindState.StartServerTime) / FMath::Max(0.001, WindState.Duration), 0., 1.);
	WindState.From = FMath::Lerp(WindState.From, WindState.To, Alpha);
	WindState.To = FVector(ServerWeatherRandomFloat(-1., 1.), ServerWeatherRandomFloat(-1., 1.), 0.);
	WindState.OffsetAtStart = Offset;
	WindState.StartServerTime = Now;
	if (UTimelineComponent* Timeline = Cast<UTimelineComponent>(ReadObject(TEXT("TL_Change Wind Dir"))))
		WindState.Duration = FMath::Max(0.001, static_cast<double>(Timeline->GetTimelineLength()) / FMath::Max(0.001f, Timeline->GetPlayRate()));
	const double Min = FMath::Max(0., ReadNumber(TEXT("WindChangeCycle Seconds Min")));
	const double Max = FMath::Max(Min, ReadNumber(TEXT("WindChangeCycle Seconds Max")));
	NextWindServerTime = Now + WindState.Duration + ServerWeatherRandomFloat(Min, Max);
	ForceNetUpdate();
}

void ASWNetworkWeatherActor::BeginPlay()
{
	Super::BeginPlay();
	bAdapterReady = ValidateAdapter();
	if (!bAdapterReady || !Invoke(TEXT("CreateDynamicMat")))
	{
		LogOnce(0, 0, TEXT("AdapterInvalid")); SetActorTickEnabled(false); return;
	}
	TInlineComponentArray<UTimelineComponent*> Timelines(this);
	for (UTimelineComponent* Timeline : Timelines) Timeline->Stop();
	for (FName Name : WeatherMaterials())
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(ReadObject(Name));
		if (!MID || !MID->Parent) { bAdapterReady = false; LogOnce(0, 0, TEXT("AdapterInvalid.Material")); SetActorTickEnabled(false); return; }
		UMaterialInstanceDynamic* Planner = UMaterialInstanceDynamic::Create(MID->Parent, this);
		Planner->CopyParameterOverrides(MID); PlannerMaterials.Add(Planner);
		UMaterialInstanceDynamic* Recovery = UMaterialInstanceDynamic::Create(MID->Parent, this);
		Recovery->CopyParameterOverrides(MID); RecoveryMaterials.Add(Recovery);
	}
	LastLocalWorldTime = GetWorld()->GetTimeSeconds(); ClockSampleStartTime = LastLocalWorldTime;
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (!ResolveComponent<UDirectionalLightComponent>(ReadObject(TEXT("DirectionalLight")))
			|| !ResolveComponent<USkyLightComponent>(ReadObject(TEXT("SkyLight")))
			|| !ResolveComponent<UExponentialHeightFogComponent>(ReadObject(TEXT("ExponentialHeightFog")))
			|| !ResolveComponent<UMeshComponent>(ReadObject(TEXT("SunBillboard"))))
		{
			bAdapterReady = false; LogOnce(0, 0, TEXT("AdapterInvalid.SceneComponents")); SetActorTickEnabled(false); return;
		}
	}
	if (HasAuthority())
	{
		InitialVoyageHour = static_cast<int32>(ReadNumber(TEXT("Init Hour")));
		InitialVoyageMinute = static_cast<int32>(ReadNumber(TEXT("Init Minute")));
		InitialVoyageWeather = static_cast<uint8>(ReadNumber(TEXT("InitWeather")));
		InitialVoyageWind.bInitialized = true; InitialVoyageWind.Speed = ReadNumber(TEXT("WindSpeed"));
		if (FStructProperty* P = FindFProperty<FStructProperty>(GetClass(), TEXT("WindDir")))
			InitialVoyageWind.From = InitialVoyageWind.To = *P->ContainerPtrToValuePtr<FVector>(this);
		bHasInitialVoyageSettings = true;
		if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
		WriteNumber(TEXT("WeatherChangeCycle"), FMath::Max(1., ReadNumber(TEXT("WeatherChangeCycle"))));
		if (!Invoke(TEXT("WeatherRaffleSetting"))) { FailGeneration(TEXT("WeatherRaffleSetting")); return; }
		SetNetworkWeatherTime(static_cast<int32>(ReadNumber(TEXT("Init Hour"))), static_cast<int32>(ReadNumber(TEXT("Init Minute"))), static_cast<uint8>(ReadNumber(TEXT("InitWeather"))));
		const double Now = GetServerTime();
		WindState.bInitialized = true; WindState.StartServerTime = Now; WindState.Speed = ReadNumber(TEXT("WindSpeed"));
		if (FStructProperty* P = FindFProperty<FStructProperty>(GetClass(), TEXT("WindDir"))) WindState.From = WindState.To = *P->ContainerPtrToValuePtr<FVector>(this);
		NextWindServerTime = Now;
	}
	else if (!PlaybackWindow.Segments.IsEmpty()) MergePlaybackWindow(PlaybackWindow);
}

void ASWNetworkWeatherActor::EndPlay(const EEndPlayReason::Type Reason)
{
	for (auto& Pair : AssetLoadHandles) if (Pair.Value) Pair.Value->CancelHandle();
	AssetLoadHandles.Empty(); CompletedAssetLoads.Empty(); FailedAssetPaths.Empty(); ReportedErrors.Empty(); SegmentReadiness.Empty(); ClockSamples.Empty();
	PlaybackBuffer.Empty(); PendingEpochBuffer.Empty(); ServerHistory.Empty(); ReadyAssets.Empty(); PlannerMaterials.Empty(); RecoveryMaterials.Empty();
	ActiveWeatherState = {}; PlannerState = {}; RecoveryTarget = {}; RecoveryNext = {}; PlaybackWindow = {};
	RecoverySourceFrame = {}; RecoveryTargetFrame = {}; LastVisualFrame = {};
	AppliedEpoch = AppliedSequence = ReceivedEpoch = 0; ReceivedPublishedTime = EpochPlaybackStartServerTime = ServerEpochStartTime = 0.;
	PlaybackServerTime = LastLocalWorldTime = LocalElapsed = PlaybackStep = RecoveryElapsed = LastPresentedAlpha = 0.; PlaybackRate = 1.;
	DiagnosticLastServerTime = DiagnosticLastLocalTime = DiagnosticLastAlpha = DiagnosticNextSampleTime = 0.; DiagnosticLastSequence = 0;
	NextClockSampleTime = ClockSampleStartTime = NextClockWarningTime = NextClockErrorTime = NextWindServerTime = 0.;
	WindState = {}; PendingVisibilityFrom = 1.f;
	bAdapterReady = bEventsReady = bGenerationFailed = bInitialWindowPublished = bHasVisualFrame = false;
	PlaybackStatus = ESWWeatherPlaybackStatus::WaitingInitialState;
	Super::EndPlay(Reason);
}

void ASWNetworkWeatherActor::SetNetworkWeatherTime(int32 Hour, int32 Minute, uint8 Weather)
{
	if (!bInitializingVoyageWeather && USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
	if (!HasAuthority() || !bAdapterReady) return;
	if (PlannerState.Epoch == MAX_uint32) { LogOnce(PlannerState.Epoch, PlannerState.Sequence, TEXT("GenerationFailure.EpochOverflow")); return; }
	const uint32 Epoch = PlannerState.Epoch + 1;
	Hour = FMath::Clamp(Hour, 0, 23); Minute = FMath::Clamp(Minute, 0, 59);
	const double Duration = ReadNumber(TEXT("1 Hour Seconds"));
	FProperty* Init = GetClass()->FindPropertyByName(TEXT("InitWeather"));
	FByteProperty* Byte = CastField<FByteProperty>(Init);
	FEnumProperty* EnumProperty = CastField<FEnumProperty>(Init);
	UEnum* Enum = Byte ? Byte->Enum.Get() : (EnumProperty ? EnumProperty->GetEnum() : nullptr);
	if (!FMath::IsFinite(Duration) || Duration < 0. || !Enum || !Enum->IsValidEnumValue(Weather)
		|| Enum->GetIndexByValue(Weather) >= Enum->NumEnums() - 1) { LogOnce(Epoch, 0, TEXT("InvalidTimeSkip")); return; }
	bGenerationFailed = false; bInitialWindowPublished = false; ServerHistory.Reset();
	WriteNumber(TEXT("InitWeather"), Weather);
	PlannerState = {}; PlannerState.Epoch = Epoch; PlannerState.Sequence = 1;
	PlannerState.Hour = Hour; PlannerState.SecondsPerHour = Duration; PlannerState.FrozenAlpha = Minute / 60.;
	const double Now = GetServerTime(); ServerEpochStartTime = Now - 2.;
	PlannerState.StartServerTime = ServerEpochStartTime - PlannerState.FrozenAlpha * Duration;
	if (!Invoke(TEXT("InitializeSetting"), {{TEXT("Hour"), static_cast<double>(Hour)}, {TEXT("Minute"), static_cast<double>(Minute)}, {TEXT("Weather"), static_cast<double>(Weather)}})) { FailGeneration(TEXT("InitializeSetting")); return; }
	PendingVisibilityFrom = static_cast<float>(ReadNumber(TEXT("SunVisible")));
	if (!CaptureSegment(PlannerState)) { FailGeneration(TEXT("InitialCapture")); return; }
	for (int32 I = 0; I < WeatherMaterials().Num(); ++I) PlannerMaterials[I]->CopyParameterOverrides(CastChecked<UMaterialInstanceDynamic>(ReadObject(WeatherMaterials()[I])));
	ServerHistory.Add(PlannerState);
	BuildFutureSegments(Now); PublishPlaybackWindow(Now);
}

bool ASWNetworkWeatherActor::CaptureSegment(FSWWeatherPlaybackSegment& Segment)
{
	Segment.Properties.Reset(); Segment.Materials.Reset();
	for (FName Name : WeatherProperties())
	{
		FProperty* P = GetClass()->FindPropertyByName(Name);
		FSWWeatherPropertyValue Entry; Entry.Name = Name;
		const void* Address = P->ContainerPtrToValuePtr<void>(this);
		FString Failure;
		if (!WalkValue(P, Address, Name, TEXT(""), Entry.References, &Failure))
		{
			LogOnce(Segment.Epoch, Segment.Sequence, TEXT("CaptureProperty.") + Failure); return false;
		}
		P->ExportTextItem_Direct(Entry.Value, Address, nullptr, this, PPF_None);
		for (const auto& R : Entry.References) if (!R.bIsNull) ReadyAssets.Add(R.ObjectPath, FindObject<UObject>(nullptr, *R.ObjectPath));
		Segment.Properties.Add(MoveTemp(Entry));
	}
	for (FName Name : WeatherMaterials())
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(ReadObject(Name));
		FSWWeatherMaterialState Material;
		if (!CaptureMaterial(MID, Name, Material)) { LogOnce(Segment.Epoch, Segment.Sequence, TEXT("CaptureMaterial.") + Name.ToString()); return false; }
		for (const auto& V : MID->TextureParameterValues) if (V.ParameterValue) ReadyAssets.Add(V.ParameterValue->GetPathName(), V.ParameterValue);
		Segment.Materials.Add(MoveTemp(Material));
	}
	Segment.SunVisibilityFrom = PendingVisibilityFrom; Segment.SunVisibilityTo = static_cast<float>(ReadNumber(TEXT("SunVisible")));
	Canonicalize(Segment);
	if (!ValidateSegment(Segment)) { LogOnce(Segment.Epoch, Segment.Sequence, TEXT("CaptureSegment.LayoutOrBounds")); return false; }
	if (!ValidatePropertyValues(Segment, false)) { LogOnce(Segment.Epoch, Segment.Sequence, TEXT("CaptureSegment.PropertyRoundTrip")); return false; }
	return true;
}

bool ASWNetworkWeatherActor::ValidatePropertyValues(const FSWWeatherPlaybackSegment& Segment, bool bApply)
{
	TArray<TUniquePtr<FPropertyScratch>> Scratch;
	for (const auto& Entry : Segment.Properties)
	{
		auto Reject = [&](const FString& Reason)
		{
			LogOnce(Segment.Epoch, Segment.Sequence, TEXT("PropertyValidation.") + Entry.Name.ToString() + TEXT(":") + Reason);
			return false;
		};
		FProperty* P = GetClass()->FindPropertyByName(Entry.Name);
		if (!P || !ValidateLayout(P)) return Reject(TEXT("Layout"));
		if (!TokenManifest(Entry)) return Reject(TEXT("TokenManifest"));
		if (!BoundedPropertyText(Entry.Value)) return Reject(TEXT("TextBounds"));
		for (const auto& R : Entry.References) if (!R.bIsNull && !ValidAsset(ReadyAssets.FindRef(R.ObjectPath), R.ObjectPath, static_cast<uint8>(R.Kind))) return Reject(TEXT("Asset=") + R.ObjectPath);
		auto Value = MakeUnique<FPropertyScratch>(P);
		if (Entry.Name == TEXT("NextWeather") || Entry.Name == TEXT("NextWeather Buf"))
		{
			const FStructProperty* Weather = CastFieldChecked<FStructProperty>(P);
			const FArrayProperty* Overrides = FindFProperty<FArrayProperty>(Weather->Struct, OverridesField);
			if (!Overrides) return Reject(TEXT("OverrideArrayLayout"));
			// ExportText omits an empty array. Remove the user-defined struct's
			// authored default entries before importing the captured value.
			FScriptArrayHelper OverrideArray(Overrides, Overrides->ContainerPtrToValuePtr<void>(Value->Value));
			OverrideArray.EmptyValues();
		}
		const TCHAR* End = P->ImportText_Direct(*Entry.Value, Value->Value, this, PPF_None);
		if (!End) return Reject(TEXT("ImportText"));
		while (FChar::IsWhitespace(*End)) ++End;
		if (*End) return Reject(TEXT("TrailingText"));
		TArray<FSWWeatherAssetReference> Actual;
		FString Failure;
		if (!WalkValue(P, Value->Value, Entry.Name, TEXT(""), Actual, &Failure))
		{
			int32 ScalarCurveReferences = 0;
			for (const auto& Reference : Entry.References) if (Reference.Kind == ESWWeatherAssetKind::ScalarCurve) ++ScalarCurveReferences;
			LogOnce(Segment.Epoch, Segment.Sequence, FString::Printf(TEXT("PropertyImportSummary.Property=%s ScalarCurveReferences=%d OverrideFieldInText=%d"), *Entry.Name.ToString(), ScalarCurveReferences, Entry.Value.Contains(OverridesField.ToString())));
			for (const auto& Reference : Entry.References)
			{
				if (!Failure.StartsWith(Entry.Name.ToString() + TEXT(".") + Reference.MemberPath + TEXT(":"))) continue;
				const int32 FieldStart = Entry.Value.Find(CurveField.ToString());
				LogOnce(Segment.Epoch, Segment.Sequence, FString::Printf(TEXT("PropertyImportDetail.Expected=%s Loaded=%d Text=%s"), *Reference.ObjectPath, ReadyAssets.Contains(Reference.ObjectPath), FieldStart == INDEX_NONE ? TEXT("<field absent>") : *Entry.Value.Mid(FieldStart, 240)));
				break;
			}
			return Reject(Failure);
		}
		if (!SameReferences(MoveTemp(Actual), Entry.References)) return Reject(TEXT("ReferenceMismatch"));
		Scratch.Add(MoveTemp(Value));
	}
	if (bApply) for (const auto& Value : Scratch) Value->Property->CopyCompleteValue(Value->Property->ContainerPtrToValuePtr<void>(this), Value->Value);
	return true;
}

bool ASWNetworkWeatherActor::RestoreSegmentValues(const FSWWeatherPlaybackSegment& Segment)
{
	// Resolve every value before touching any displayed property or MID.
	for (const auto& M : Segment.Materials)
	{
		if (!Cast<UMaterialInstanceDynamic>(ReadObject(M.PropertyName))) return false;
		for (const auto& T : M.Textures) if (!T.bIsNull && !ValidAsset(ReadyAssets.FindRef(T.ObjectPath), T.ObjectPath, 3)) return false;
	}
	if (!ValidateSegment(Segment) || !ValidatePropertyValues(Segment, true)) return false;
	for (const auto& M : Segment.Materials)
	{
		UMaterialInstanceDynamic* MID = CastChecked<UMaterialInstanceDynamic>(ReadObject(M.PropertyName));
		MID->ClearParameterValues();
		for (const auto& V : M.Scalars) MID->SetScalarParameterValue(V.Name, V.Value);
		for (const auto& V : M.Vectors) MID->SetVectorParameterValue(V.Name, V.Value);
		for (const auto& V : M.Textures) MID->SetTextureParameterValue(V.Name, V.bIsNull ? nullptr : CastChecked<UTexture2D>(ReadyAssets.FindRef(V.ObjectPath)));
	}
	return true;
}

void ASWNetworkWeatherActor::FailGeneration(const TCHAR* Reason)
{
	bGenerationFailed = true;
	LogOnce(PlannerState.Epoch, PlannerState.Sequence, FString(TEXT("GenerationFailure.")) + Reason);
}

void ASWNetworkWeatherActor::BuildFutureSegments(double Now)
{
	if (!HasAuthority() || bGenerationFailed || !PlannerState.Sequence || PlannerState.SecondsPerHour == 0.) return;
	const double Horizon = Now + 4.;
	{
		FWeatherContext Context(this, &PendingVisibilityFrom);
		for (int32 I = 0; I < 3; ++I) FindFProperty<FObjectPropertyBase>(GetClass(), WeatherMaterials()[I])->SetObjectPropertyValue_InContainer(this, PlannerMaterials[I]);
		int32 Steps = 0;
		while ((PlannerState.StartServerTime + PlannerState.SecondsPerHour <= Horizon || PlannerState.StartServerTime <= Now - 2.) && Steps++ < 32)
		{
			if (PlannerState.Sequence == MAX_uint32) { FailGeneration(TEXT("SequenceOverflow")); break; }
			if (!RestoreSegmentValues(PlannerState) || !Invoke(TEXT("ChangeMatParam"), {{TEXT("InputAlpha"), 1.}})) { FailGeneration(TEXT("PlannerRestore")); break; }
			FSWWeatherPlaybackSegment Next = PlannerState;
			Next.Sequence++; Next.Hour = (Next.Hour + 1) % 24; Next.StartServerTime += Next.SecondsPerHour;
			PendingVisibilityFrom = PlannerState.SunVisibilityTo;
			WriteNumber(TEXT("CurrentHour"), Next.Hour); WriteNumber(TEXT("Current Minute"), 0.);
			if (!Invoke(TEXT("SetNextWeather"), {{TEXT("NextHour"), static_cast<double>(Next.Hour)}}) || !CaptureSegment(Next)) { FailGeneration(TEXT("PlannerCapture")); break; }
			PlannerState = MoveTemp(Next); ServerHistory.Add(PlannerState);
			if (ServerHistory.Num() > 256)
			{
				const auto& Oldest = ServerHistory[0];
				const bool bProtected = (AppliedEpoch == Oldest.Epoch && AppliedSequence == Oldest.Sequence)
					|| Oldest.StartServerTime + Oldest.SecondsPerHour > Now - 2.;
				if (bProtected) { FailGeneration(TEXT("MinimumWindowCapacity")); break; }
				ServerHistory.RemoveAt(0); LogOnce(PlannerState.Epoch, PlannerState.Sequence, TEXT("BufferHistoryLost.CountLimit"));
			}
		}
	}
}

void ASWNetworkWeatherActor::PublishPlaybackWindow(double Now)
{
	if (bGenerationFailed || ServerHistory.IsEmpty()) return;
	if (!bInitialWindowPublished && PlannerState.SecondsPerHour > 0.
		&& (PlannerState.StartServerTime + PlannerState.SecondsPerHour <= Now + 4. || PlannerState.StartServerTime <= Now - 2.)) return;
	while (ServerHistory.Num() > 1 && ServerHistory[0].SecondsPerHour > 0.
		&& ServerHistory[0].StartServerTime + ServerHistory[0].SecondsPerHour < Now - 122.
		&& !(AppliedEpoch == ServerHistory[0].Epoch && AppliedSequence == ServerHistory[0].Sequence)) ServerHistory.RemoveAt(0);
	FSWWeatherPlaybackWindow Candidate; Candidate.Epoch = PlannerState.Epoch; Candidate.PublishedServerTime = Now;
	const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	Candidate.Generation = Voyage ? Voyage->GetGeneration() : 0;
	Candidate.EpochPlaybackStartServerTime = ServerEpochStartTime; Candidate.Segments = ServerHistory;
	while (!Candidate.PreparePayload())
	{
		if (Candidate.Segments.Num() <= 2 || Candidate.Segments[0].StartServerTime + Candidate.Segments[0].SecondsPerHour > Now - 2.
			|| (AppliedEpoch == Candidate.Epoch && AppliedSequence == Candidate.Segments[0].Sequence)) { FailGeneration(TEXT("MinimumWireSize")); return; }
		Candidate.Segments.RemoveAt(0); LogOnce(Candidate.Epoch, Candidate.Segments[0].Sequence, TEXT("BufferHistoryLost.WireSizeLimit"));
	}
	if (PlaybackWindow.Epoch == Candidate.Epoch && PlaybackWindow.Segments.Num() == Candidate.Segments.Num()
		&& PlaybackWindow.Segments[0].Sequence == Candidate.Segments[0].Sequence
		&& PlaybackWindow.Segments.Last().Sequence == Candidate.Segments.Last().Sequence)
	{
		// Revisit deferred far-future entries without manufacturing a new network publication.
		if (GetNetMode() != NM_DedicatedServer) MergePlaybackWindow(PlaybackWindow);
		return;
	}
	ServerHistory = Candidate.Segments; PlaybackWindow = MoveTemp(Candidate); bInitialWindowPublished = true;
	ForceNetUpdate();
	if (GetNetMode() != NM_DedicatedServer) MergePlaybackWindow(PlaybackWindow);
}

void ASWNetworkWeatherActor::OnRep_PlaybackWindow()
{
	MergePlaybackWindow(PlaybackWindow);
}

void ASWNetworkWeatherActor::MergePlaybackWindow(const FSWWeatherPlaybackWindow& Window)
{
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	const int32 Generation = Voyage ? Voyage->GetGeneration() : 0;
	if (Window.Generation < Generation) return;
	if (Window.Generation > Generation)
	{
		if (Window.Generation > FutureVoyageWindow.Generation
			|| (Window.Generation == FutureVoyageWindow.Generation && Window.PublishedServerTime >= FutureVoyageWindow.PublishedServerTime))
			FutureVoyageWindow = Window;
		return;
	}
	if (Window.Segments.IsEmpty() || Window.Epoch < ReceivedEpoch || (Window.Epoch == ReceivedEpoch && Window.PublishedServerTime < ReceivedPublishedTime)) return;
	if (!ValidateWindow(Window)) { LogOnce(Window.Epoch, 0, TEXT("ReceivedWindowInvalid")); return; }
	TArray<FSWWeatherPlaybackSegment> Incoming = Window.Segments;
	for (auto& Segment : Incoming)
	{
		Canonicalize(Segment);
		for (const auto& P : Segment.Properties) if (!TokenManifest(P) || !BoundedPropertyText(P.Value)) { LogOnce(Segment.Epoch, Segment.Sequence, TEXT("InvalidReferenceManifest")); return; }
		const auto* Existing = PlaybackBuffer.FindByPredicate([&Segment](const auto& S) { return SegmentKey(S) == SegmentKey(Segment); });
		if (!Existing) Existing = PendingEpochBuffer.FindByPredicate([&Segment](const auto& S) { return SegmentKey(S) == SegmentKey(Segment); });
		if ((Existing && !SameSegment(*Existing, Segment)) || (SegmentKey(ActiveWeatherState) == SegmentKey(Segment) && !SameSegment(ActiveWeatherState, Segment)))
		{
			LogOnce(Segment.Epoch, Segment.Sequence, TEXT("InvalidSegment.ConflictingID")); return;
		}
		for (const auto* Reserved : {&RecoveryTarget, &RecoveryNext})
			if (Reserved->Sequence && SegmentKey(*Reserved) == SegmentKey(Segment) && !SameSegment(*Reserved, Segment))
			{
				LogOnce(Segment.Epoch, Segment.Sequence, TEXT("InvalidSegment.ConflictingRecoveryID")); return;
			}
	}
	if (Window.Epoch > ReceivedEpoch)
	{
		// Keep the presented epoch intact until the new epoch's assets are ready.
		PendingEpochBuffer.Reset();
		if (!AppliedEpoch) PlaybackBuffer.Reset();
		ReceivedEpoch = Window.Epoch; FailedAssetPaths.Reset();
		if (AppliedEpoch) PlaybackStatus = ESWWeatherPlaybackStatus::WaitingEpochReset;
	}
	ReceivedPublishedTime = Window.PublishedServerTime; EpochPlaybackStartServerTime = Window.EpochPlaybackStartServerTime;
	PlaybackBuffer.RemoveAll([this](const auto& S) { return S.Epoch == AppliedEpoch && S.Sequence < AppliedSequence; });
	TArray<FSWWeatherPlaybackSegment>& Destination = AppliedEpoch && ReceivedEpoch > AppliedEpoch ? PendingEpochBuffer : PlaybackBuffer;
	for (const auto& S : Incoming)
	{
		if (S.Epoch == AppliedEpoch && S.Sequence < AppliedSequence) continue;
		if (Destination.ContainsByPredicate([&S](const auto& X) { return SegmentKey(X) == SegmentKey(S); })) continue;
		if (Destination.Num() >= 256) { LogOnce(S.Epoch, S.Sequence, TEXT("BufferCapacityExceeded")); break; }
		Destination.Add(S); SegmentReadiness.Add(SegmentKey(S), ESWWeatherSegmentReadiness::PendingAssets);
	}
	Destination.Sort([](const auto& A, const auto& B) { return SegmentKey(A) < SegmentKey(B); });
	if (AppliedEpoch == Window.Epoch && AppliedSequence && !PlaybackBuffer.ContainsByPredicate([this](const auto& S) { return S.Epoch == AppliedEpoch && S.Sequence == AppliedSequence + 1; })
		&& Window.Segments[0].Sequence > AppliedSequence + 1)
	{
		const double Target = GetServerTime() - 2.;
		for (int32 I = 0; I < Incoming.Num(); ++I)
		{
			const auto& S = Incoming[I];
			if (S.StartServerTime <= Target && (S.SecondsPerHour == 0. || Target < S.StartServerTime + S.SecondsPerHour) && (S.SecondsPerHour == 0. || I + 1 < Incoming.Num()))
			{
				if (PlaybackStatus != ESWWeatherPlaybackStatus::Recovering) { RecoveryTarget = S; RecoveryNext = S.SecondsPerHour == 0. ? FSWWeatherPlaybackSegment() : Incoming[I + 1]; }
				break;
			}
		}
	}
}

bool ASWNetworkWeatherActor::IsReady(const FSWWeatherPlaybackSegment& Segment) const
{
	const auto* Ready = SegmentReadiness.Find(SegmentKey(Segment)); return Ready && *Ready == ESWWeatherSegmentReadiness::Ready;
}

void ASWNetworkWeatherActor::PrepareSegments()
{
	for (const FString& Path : CompletedAssetLoads)
	{
		UObject* Object = FindObject<UObject>(nullptr, *Path);
		uint8 Kind = 0; for (; Kind < 4 && !Path.StartsWith(AssetPrefix(Kind), ESearchCase::CaseSensitive); ++Kind) {}
		if (ValidAsset(Object, Path, Kind)) ReadyAssets.Add(Path, Object);
		else { FailedAssetPaths.Add(Path); LogOnce(ReceivedEpoch, 0, TEXT("LoadFailed.InvalidReference:") + Path); }
		AssetLoadHandles.Remove(Path);
	}
	CompletedAssetLoads.Reset();
	TArray<FSWWeatherPlaybackSegment*> Pending;
	for (auto& S : PlaybackBuffer) Pending.Add(&S);
	for (auto& S : PendingEpochBuffer) Pending.Add(&S);
	if (RecoveryTarget.Sequence) Pending.Add(&RecoveryTarget);
	if (RecoveryNext.Sequence) Pending.Add(&RecoveryNext);
	for (auto* S : Pending)
	{
		if (IsReady(*S) || SegmentReadiness.FindRef(SegmentKey(*S)) == ESWWeatherSegmentReadiness::Invalid) continue;
		TArray<FString> Paths;
		for (const auto& P : S->Properties) for (const auto& R : P.References) if (!R.bIsNull) Paths.AddUnique(R.ObjectPath);
		for (const auto& M : S->Materials) for (const auto& T : M.Textures) if (!T.bIsNull) Paths.AddUnique(T.ObjectPath);
		bool bWaiting = false, bInvalid = false;
		for (const FString& Path : Paths)
		{
			if (ReadyAssets.Contains(Path)) continue;
			if (FailedAssetPaths.Contains(Path)) { bInvalid = true; continue; }
			bWaiting = true;
			if (AssetLoadHandles.Contains(Path)) continue;
			const TWeakObjectPtr<ASWNetworkWeatherActor> WeakThis(this);
			USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
			const TSharedPtr<FSWVoyageAsyncGuard, ESPMode::ThreadSafe> Guard = Voyage ? Voyage->GetAsyncGuard() : nullptr;
			TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(FSoftObjectPath(Path), FStreamableDelegate::CreateLambda([WeakThis, Path, Guard]()
			{
				if (Guard && Guard->bCancelled.Load()) return;
				if (ASWNetworkWeatherActor* Actor = WeakThis.Get()) Actor->CompletedAssetLoads.Add(Path);
			}));
			if (Handle) AssetLoadHandles.Add(Path, Handle);
			else { FailedAssetPaths.Add(Path); bInvalid = true; }
		}
		if (bInvalid) { SegmentReadiness.Add(SegmentKey(*S), ESWWeatherSegmentReadiness::Invalid); LogOnce(S->Epoch, S->Sequence, TEXT("InvalidReference")); }
		else if (!bWaiting)
		{
			const bool bValid = ValidatePropertyValues(*S, false);
			SegmentReadiness.Add(SegmentKey(*S), bValid ? ESWWeatherSegmentReadiness::Ready : ESWWeatherSegmentReadiness::Invalid);
			if (!bValid) LogOnce(S->Epoch, S->Sequence, TEXT("InvalidPropertyText"));
		}
	}
}

bool ASWNetworkWeatherActor::EnterSegment(const FSWWeatherPlaybackSegment& Segment)
{
	if (!IsReady(Segment) || !RestoreSegmentValues(Segment)) return false;
	ActiveWeatherState = Segment; AppliedEpoch = Segment.Epoch; AppliedSequence = Segment.Sequence; LastPresentedAlpha = 0.;
	if (!bEventsReady) { if (!Invoke(TEXT("WeatherEventCall Setup"))) return false; bEventsReady = true; }
	if (!Invoke(TEXT("WeatherEventCall"))) return false;
	PlaybackBuffer.RemoveAll([this](const auto& S) { return S.Epoch != AppliedEpoch || S.Sequence < AppliedSequence; });
	return true;
}

bool ASWNetworkWeatherActor::TryStartPlayback(double Now, bool bEpochReset)
{
	if (!GetWorld()->GetGameState()) return false;
	if (!bEpochReset)
	{
		const double LocalTime = GetWorld()->GetTimeSeconds();
		const double SampleTime = FPlatformTime::Seconds();
		if (SampleTime >= NextClockSampleTime)
		{
			ClockSamples.Emplace(SampleTime, Now - LocalTime); NextClockSampleTime = SampleTime + 0.1;
			while (ClockSamples.Num() > 1 && ClockSamples[1].Key <= SampleTime - 2.) ClockSamples.RemoveAt(0);
		}
		bool bStable = ClockSamples.Num() > 1 && ClockSamples.Last().Key - ClockSamples[0].Key >= 2.;
		double Min = DBL_MAX, Max = -DBL_MAX;
		for (const auto& Sample : ClockSamples) { Min = FMath::Min(Min, Sample.Value); Max = FMath::Max(Max, Sample.Value); }
		bStable &= Max - Min <= 0.05;
		if (!bStable)
		{
			if (LocalTime - ClockSampleStartTime >= 10. && LocalTime >= NextClockWarningTime) { LogWeatherDiagnostics(TEXT("ClockUnstable"), Now); NextClockWarningTime = LocalTime + 5.; }
			return false;
		}
	}
	const double Target = FMath::Max(Now - 2., EpochPlaybackStartServerTime);
	TArray<FSWWeatherPlaybackSegment>& Candidates = bEpochReset ? PendingEpochBuffer : PlaybackBuffer;
	for (const auto& S : Candidates)
	{
		if (S.Epoch != ReceivedEpoch || !IsReady(S) || S.StartServerTime > Target || (S.SecondsPerHour > 0. && Target >= S.StartServerTime + S.SecondsPerHour)) continue;
		const auto* Next = Candidates.FindByPredicate([&S](const auto& N) { return N.Epoch == S.Epoch && N.Sequence == S.Sequence + 1; });
		if (S.SecondsPerHour > 0. && (!Next || !IsReady(*Next) || FMath::Abs(Next->StartServerTime - S.StartServerTime - S.SecondsPerHour) > BoundaryTolerance)) return false;
		const FSWWeatherPlaybackSegment Start = S;
		if (bEpochReset) { PlaybackBuffer = PendingEpochBuffer; PendingEpochBuffer.Reset(); }
		if (!EnterSegment(Start)) return false;
		PlaybackServerTime = Start.SecondsPerHour > 0. ? Target : Start.StartServerTime;
		PlaybackRate = 1.; PlaybackStatus = Start.SecondsPerHour > 0. ? ESWWeatherPlaybackStatus::Playing : ESWWeatherPlaybackStatus::Frozen;
		RecoveryTarget = {}; RecoveryNext = {}; RecoverySourceFrame = {}; RecoveryTargetFrame = {}; RecoveryElapsed = 0.; bHasVisualFrame = false;
		LogWeatherDiagnostics(bEpochReset ? TEXT("EpochReset") : TEXT("PlaybackStart"), Now); return true;
	}
	return false;
}

void ASWNetworkWeatherActor::EvaluatePresentedWeather()
{
	const auto& S = ActiveWeatherState;
	if (!S.Sequence) return;
	const double Alpha = S.SecondsPerHour > 0. ? FMath::Clamp((PlaybackServerTime - S.StartServerTime) / S.SecondsPerHour, 0., 1.) : S.FrozenAlpha;
	if (Alpha + 1.e-9 < LastPresentedAlpha) LogOnce(S.Epoch, S.Sequence, TEXT("AlphaRegression"));
	WriteNumber(TEXT("CurrentHour"), S.Hour); WriteNumber(TEXT("Current Minute"), FMath::Min(59, FMath::FloorToInt(Alpha * 60.)));
	Invoke(TEXT("Change Param"), {{TEXT("time"), Alpha}});
	if (auto* Parameters = GetWorld()->GetParameterCollectionInstance(SkyParameters))
	{
		float Visibility = S.SunVisibilityTo;
		if (S.SunVisibilityFrom != S.SunVisibilityTo) Visibility = SunVisibilityCurve
			? SunVisibilityCurve->GetFloatValue(static_cast<float>(Visibility == 0.f ? Alpha : 1. - Alpha)) : FMath::Lerp(S.SunVisibilityFrom, S.SunVisibilityTo, static_cast<float>(Alpha));
		Parameters->SetScalarParameterValue(TEXT("SunVisible"), Visibility);
	}
	LastPresentedAlpha = Alpha;
}

void ASWNetworkWeatherActor::AdvanceAcrossSegments(double Step)
{
	if (PlaybackStatus == ESWWeatherPlaybackStatus::Frozen || !ActiveWeatherState.Sequence) return;
	for (int32 Count = 0; Count < 256; ++Count)
	{
		const double End = ActiveWeatherState.StartServerTime + ActiveWeatherState.SecondsPerHour;
		const double Remaining = FMath::Max(0., End - PlaybackServerTime);
		if (Step < Remaining) { PlaybackServerTime += Step; return; }
		PlaybackServerTime = End; Step -= Remaining;
		EvaluatePresentedWeather();
		FSWWeatherVisualFrame BoundaryFrame; CaptureVisualFrame(BoundaryFrame);
		const auto* Next = PlaybackBuffer.FindByPredicate([this](const auto& S) { return S.Epoch == AppliedEpoch && S.Sequence == AppliedSequence + 1; });
		if (!Next || !IsReady(*Next) || FMath::Abs(Next->StartServerTime - End) > BoundaryTolerance)
		{
			if (PlaybackStatus != ESWWeatherPlaybackStatus::HoldingForData && PlaybackStatus != ESWWeatherPlaybackStatus::WaitingRecoveryTarget)
			{
				PlaybackStatus = ESWWeatherPlaybackStatus::HoldingForData; LogWeatherDiagnostics(TEXT("BufferHold"), GetServerTime());
			}
			if (!Next && PlaybackWindow.Epoch == AppliedEpoch && !PlaybackWindow.Segments.IsEmpty() && PlaybackWindow.Segments[0].Sequence > AppliedSequence + 1)
			{
				PlaybackStatus = ESWWeatherPlaybackStatus::WaitingRecoveryTarget; LogOnce(AppliedEpoch, AppliedSequence, TEXT("BufferHistoryLost"));
			}
			return;
		}
		const bool bResuming = PlaybackStatus == ESWWeatherPlaybackStatus::HoldingForData;
		const FSWWeatherPlaybackSegment Incoming = *Next;
		if (!EnterSegment(Incoming)) { PlaybackStatus = ESWWeatherPlaybackStatus::HoldingForData; return; }
		PlaybackServerTime = Incoming.StartServerTime; PlaybackStatus = ESWWeatherPlaybackStatus::Playing;
		EvaluatePresentedWeather();
		FSWWeatherVisualFrame StartFrame;
		if (CaptureVisualFrame(StartFrame)) CompareVisualFrames(TEXT("BoundaryDiscontinuity"), BoundaryFrame, StartFrame);
		LogWeatherDiagnostics(bResuming ? TEXT("BufferResume") : TEXT("BoundaryStart"), GetServerTime());
		// A held frame starts exactly at zero, without spending any time accumulated while holding.
		if (bResuming || Step <= 0.) return;
	}
	PlaybackStatus = ESWWeatherPlaybackStatus::HoldingForData;
	LogOnce(AppliedEpoch, AppliedSequence, TEXT("BufferHold.BoundaryLimit"));
}

bool ASWNetworkWeatherActor::CaptureVisualFrame(FSWWeatherVisualFrame& Frame)
{
	Frame = {};
	for (FName Name : WeatherMaterials())
	{
		FSWWeatherMaterialState M; if (!CaptureMaterial(Cast<UMaterialInstanceDynamic>(ReadObject(Name)), Name, M)) return false;
		Frame.Materials.Add(MoveTemp(M));
	}
	auto* Directional = ResolveComponent<UDirectionalLightComponent>(ReadObject(TEXT("DirectionalLight")));
	auto* Sky = ResolveComponent<USkyLightComponent>(ReadObject(TEXT("SkyLight")));
	auto* Fog = ResolveComponent<UExponentialHeightFogComponent>(ReadObject(TEXT("ExponentialHeightFog")));
	auto* Billboard = ResolveComponent<UMeshComponent>(ReadObject(TEXT("SunBillboard")));
	auto* Parameters = GetWorld()->GetParameterCollectionInstance(SkyParameters);
	if (!Directional || !Sky || !Fog || !Billboard || !Parameters) return false;
	Frame.DirectionalColor = Directional->GetLightColor(); Frame.SkyLightColor = Sky->GetLightColor();
	Frame.FogColor = Fog->FogInscatteringLuminance; Frame.DirectionalFogColor = Fog->DirectionalInscatteringLuminance;
	Frame.DirectionalRotation = Directional->GetComponentQuat(); Frame.BillboardLocation = Billboard->GetComponentLocation();
	Frame.BillboardScale = Billboard->GetRelativeScale3D(); Frame.BillboardMaterial = Billboard->GetMaterial(0);
	Parameters->GetScalarParameterValue(TEXT("SunVisible"), Frame.SunVisible); Parameters->GetScalarParameterValue(TEXT("Darkness"), Frame.Darkness);
	return true;
}

bool ASWNetworkWeatherActor::EvaluateVisualFrame(const FSWWeatherPlaybackSegment& Segment, double Alpha, FSWWeatherVisualFrame& Frame)
{
	FWeatherContext Context(this);
	for (int32 I = 0; I < 3; ++I) FindFProperty<FObjectPropertyBase>(GetClass(), WeatherMaterials()[I])->SetObjectPropertyValue_InContainer(this, RecoveryMaterials[I]);
	if (!RestoreSegmentValues(Segment) || !Invoke(TEXT("ChangeMatParam"), {{TEXT("InputAlpha"), Alpha}})) return false;
	Frame = {};
	for (FName Name : WeatherMaterials())
	{
		FSWWeatherMaterialState M; if (!CaptureMaterial(Cast<UMaterialInstanceDynamic>(ReadObject(Name)), Name, M)) return false;
		Frame.Materials.Add(MoveTemp(M));
	}
	for (FName Name : {FName(TEXT("AngleClamp")), FName(TEXT("SphereSize")), FName(TEXT("SunSize")), FName(TEXT("MoonSize")), FName(TEXT("BaseScale")), FName(TEXT("RiseTime(DebugParam)")), FName(TEXT("DownTime(DebugParam)"))})
		if (!FindFProperty<FNumericProperty>(GetClass(), Name) || !FMath::IsFinite(ReadNumber(Name))) return false;
	UCurveFloat* Offset = Cast<UCurveFloat>(ReadObject(TEXT("SunRizeOffset")));
	UCurveFloat* Angle = Cast<UCurveFloat>(ReadObject(TEXT("SunAngle")));
	UCurveFloat* Darkness = Cast<UCurveFloat>(ReadObject(TEXT("DarknessParam")));
	UMaterialInterface* SunMaterial = Cast<UMaterialInterface>(ReadObject(TEXT("SunMaterial")));
	UMaterialInterface* MoonMaterial = Cast<UMaterialInterface>(ReadObject(TEXT("MoonMaterial")));
	FStructProperty* RotationProperty = FindFProperty<FStructProperty>(GetClass(), TEXT("RotationSetting"));
	if (!Offset || !Angle || !Darkness || !SunMaterial || !MoonMaterial || !RotationProperty || RotationProperty->Struct != TBaseStructure<FRotator>::Get()) return false;
	const FRotator Rotation = *RotationProperty->ContainerPtrToValuePtr<FRotator>(this);
	if (Rotation.ContainsNaN()) return false;
	const float U = Offset->GetFloatValue(static_cast<float>((Segment.Hour + Alpha) / 24.));
	if (!FMath::IsFinite(U)) return false;
	auto Color = [this, U, Alpha](FName From, FName To, FLinearColor& Result)
	{
		const UCurveLinearColor* A = Cast<UCurveLinearColor>(ReadObject(From)); const UCurveLinearColor* B = Cast<UCurveLinearColor>(ReadObject(To));
		if (!A || !B) return false;
		Result = FMath::Lerp(A->GetLinearColorValue(U), B->GetLinearColorValue(U), static_cast<float>(Alpha)); return FiniteColor(Result);
	};
	FLinearColor Overall;
	if (!Color(TEXT("CurrentOverallColor"), TEXT("NextOverallColor"), Overall)
		|| !Color(TEXT("CurrentSunColor"), TEXT("NextSunColor"), Frame.DirectionalColor)
		|| !Color(TEXT("CurrentSkyLightColor"), TEXT("NextSkyLightColor"), Frame.SkyLightColor)
		|| !Color(TEXT("CurrentFogColor"), TEXT("NextFogColor"), Frame.FogColor)) return false;
	Frame.DirectionalColor *= Overall; Frame.SkyLightColor *= Overall; Frame.FogColor *= Overall;
	const float Glow = CastChecked<UMaterialInstanceDynamic>(ReadObject(TEXT("CloudMaterial")))->K2_GetScalarParameterValue(TEXT("SunGlowEmissive"));
	Frame.DirectionalFogColor = Frame.DirectionalColor * (Glow / 20.f); Frame.Darkness = Darkness->GetFloatValue(U);
	Frame.SunVisible = Segment.SunVisibilityTo;
	if (Segment.SunVisibilityFrom != Segment.SunVisibilityTo) Frame.SunVisible = SunVisibilityCurve
		? SunVisibilityCurve->GetFloatValue(static_cast<float>(Segment.SunVisibilityTo == 0.f ? Alpha : 1. - Alpha)) : FMath::Lerp(Segment.SunVisibilityFrom, Segment.SunVisibilityTo, static_cast<float>(Alpha));
	if (Segment.SecondsPerHour > 0. && Alpha == 0.) Frame.SunVisible = Segment.SunVisibilityFrom;
	const double SunAngle = Angle->GetFloatValue(U), Clamp = ReadNumber(TEXT("AngleClamp"));
	if (!FMath::IsFinite(SunAngle) || Clamp < 0. || !FMath::IsFinite(Frame.Darkness) || !FMath::IsFinite(Frame.SunVisible) || !FMath::IsFinite(Glow)) return false;
	const FQuat Base = Rotation.Quaternion(); const FVector Axis = Base.GetUpVector();
	Frame.UnclampedSunRotation = (FQuat(Axis, FMath::DegreesToRadians(SunAngle)) * Base).GetNormalized();
	Frame.DirectionalRotation = (FQuat(Axis, FMath::DegreesToRadians(FMath::Clamp(SunAngle, -Clamp, Clamp))) * Base).GetNormalized();
	Frame.BillboardLocation = Frame.UnclampedSunRotation.GetForwardVector() * ReadNumber(TEXT("SphereSize")) * -0.5;
	const double WorldTime = Segment.Hour + Alpha;
	const bool bSun = WorldTime >= ReadNumber(TEXT("RiseTime(DebugParam)")) && WorldTime <= ReadNumber(TEXT("DownTime(DebugParam)"));
	Frame.BillboardMaterial = bSun ? SunMaterial : MoonMaterial;
	Frame.BillboardScale = FVector(ReadNumber(bSun ? TEXT("SunSize") : TEXT("MoonSize")) * ReadNumber(TEXT("BaseScale")));
	return FiniteColor(Frame.DirectionalColor) && FiniteColor(Frame.SkyLightColor) && FiniteColor(Frame.FogColor) && FiniteColor(Frame.DirectionalFogColor)
		&& !Frame.BillboardLocation.ContainsNaN() && !Frame.BillboardScale.ContainsNaN();
}

bool ASWNetworkWeatherActor::TryStartRecovery(double Now)
{
	if (PlaybackStatus != ESWWeatherPlaybackStatus::WaitingRecoveryTarget || !IsReady(RecoveryTarget)
		|| (RecoveryTarget.SecondsPerHour > 0. && (!IsReady(RecoveryNext) || RecoveryNext.Sequence != RecoveryTarget.Sequence + 1))) return false;
	const double TargetTime = Now - 2.;
	if (RecoveryTarget.StartServerTime > TargetTime || (RecoveryTarget.SecondsPerHour > 0. && TargetTime >= RecoveryTarget.StartServerTime + RecoveryTarget.SecondsPerHour)) return false;
	FSWWeatherVisualFrame ExpectedSource, Source, Target;
	if (!EvaluateVisualFrame(ActiveWeatherState, ActiveWeatherState.SecondsPerHour > 0. ? 1. : ActiveWeatherState.FrozenAlpha, ExpectedSource)
		|| !CaptureVisualFrame(Source) || !EvaluateVisualFrame(RecoveryTarget, RecoveryTarget.SecondsPerHour > 0. ? 0. : RecoveryTarget.FrozenAlpha, Target))
	{
		LogOnce(AppliedEpoch, AppliedSequence, TEXT("RecoveryEvaluationFailed")); return false;
	}
	FLinearColor ActualDirection;
	GetWorld()->GetParameterCollectionInstance(SkyParameters)->GetVectorParameterValue(TEXT("SunDir"), ActualDirection);
	const FVector ExpectedDirection = ExpectedSource.UnclampedSunRotation.GetForwardVector();
	if (FMath::Max3(FMath::Abs(ActualDirection.R - ExpectedDirection.X), FMath::Abs(ActualDirection.G - ExpectedDirection.Y), FMath::Abs(ActualDirection.B - ExpectedDirection.Z)) > 0.01)
	{
		LogOnce(AppliedEpoch, AppliedSequence, TEXT("RecoveryEndpointMismatch.SunDir")); return false;
	}
	Source.UnclampedSunRotation = ExpectedSource.UnclampedSunRotation;
	for (int32 I = 0; I < Source.Materials.Num(); ++I)
	{
		auto& A = Source.Materials[I]; auto& B = Target.Materials[I];
		UMaterialInstanceDynamic* MID = CastChecked<UMaterialInstanceDynamic>(ReadObject(A.PropertyName));
		TSet<FName> Scalars, Vectors, Textures;
		for (const auto& V : A.Scalars) Scalars.Add(V.Name); for (const auto& V : B.Scalars) Scalars.Add(V.Name);
		for (const auto& V : A.Vectors) Vectors.Add(V.Name); for (const auto& V : B.Vectors) Vectors.Add(V.Name);
		for (const auto& V : A.Textures) Textures.Add(V.Name); for (const auto& V : B.Textures) Textures.Add(V.Name);
		bool bSupported = true;
		for (FName Name : Scalars)
		{
			for (auto* M : {&A, &B}) if (!M->Scalars.ContainsByPredicate([Name](const auto& V) { return V.Name == Name; }))
			{
				float Value = 0.f; if (!MID->Parent->GetScalarParameterValue(FMaterialParameterInfo(Name), Value) || !FMath::IsFinite(Value)) { bSupported = false; break; }
				FSWWeatherScalarValue V; V.Name = Name; V.Value = Value; M->Scalars.Add(V);
			}
		}
		for (FName Name : Vectors)
		{
			for (auto* M : {&A, &B}) if (!M->Vectors.ContainsByPredicate([Name](const auto& V) { return V.Name == Name; }))
			{
				FLinearColor Value; if (!MID->Parent->GetVectorParameterValue(FMaterialParameterInfo(Name), Value) || !FiniteColor(Value)) { bSupported = false; break; }
				FSWWeatherVectorValue V; V.Name = Name; V.Value = Value; M->Vectors.Add(V);
			}
		}
		for (FName Name : Textures)
		{
			if (Name == TEXT("SkyColor_Texture") || Name == TEXT("BlendBroker0")) continue;
			auto Texture = [MID, Name, &bSupported](const FSWWeatherMaterialState& M) -> UTexture*
			{
				if (const auto* T = M.Textures.FindByPredicate([Name](const auto& V) { return V.Name == Name; })) return T->bIsNull ? nullptr : FindObject<UTexture>(nullptr, *T->ObjectPath);
				UTexture* Result = nullptr; if (!MID->Parent->GetTextureParameterValue(FMaterialParameterInfo(Name), Result)) bSupported = false; return Result;
			};
			if (Texture(A) != Texture(B)) bSupported = false;
		}
		if (!bSupported) { LogOnce(RecoveryTarget.Epoch, RecoveryTarget.Sequence, TEXT("UnsupportedRecoveryParameter")); return false; }
		A.Scalars.Sort([](const auto& X, const auto& Y) { return X.Name.LexicalLess(Y.Name); }); B.Scalars.Sort([](const auto& X, const auto& Y) { return X.Name.LexicalLess(Y.Name); });
		A.Vectors.Sort([](const auto& X, const auto& Y) { return X.Name.LexicalLess(Y.Name); }); B.Vectors.Sort([](const auto& X, const auto& Y) { return X.Name.LexicalLess(Y.Name); });
	}
	RecoverySourceFrame = MoveTemp(Source); RecoveryTargetFrame = MoveTemp(Target); RecoveryElapsed = 0.;
	PlaybackStatus = ESWWeatherPlaybackStatus::Recovering; LogWeatherDiagnostics(TEXT("RecoveryStart"), Now); return true;
}

void ASWNetworkWeatherActor::AdvanceRecovery(double Step)
{
	RecoveryElapsed = FMath::Min(2., RecoveryElapsed + Step);
	const float Beta = static_cast<float>(RecoveryElapsed / 2.);
	const auto& A = RecoverySourceFrame; const auto& B = RecoveryTargetFrame;
	for (int32 I = 0; I < A.Materials.Num(); ++I)
	{
		const auto& From = A.Materials[I]; const auto& To = B.Materials[I];
		UMaterialInstanceDynamic* MID = CastChecked<UMaterialInstanceDynamic>(ReadObject(From.PropertyName));
		for (int32 J = 0; J < From.Scalars.Num(); ++J) if (From.Scalars[J].Name != TEXT("Blend0")) MID->SetScalarParameterValue(From.Scalars[J].Name, FMath::Lerp(From.Scalars[J].Value, To.Scalars[J].Value, Beta));
		for (int32 J = 0; J < From.Vectors.Num(); ++J) MID->SetVectorParameterValue(From.Vectors[J].Name, FMath::Lerp(From.Vectors[J].Value, To.Vectors[J].Value, Beta));
		if (From.PropertyName == TEXT("CurrentDynamicParam")) continue;
		auto Texture = [this](const FSWWeatherMaterialState& M, FName Name) -> UTexture*
		{
			const auto* V = M.Textures.FindByPredicate([Name](const auto& T) { return T.Name == Name; });
			return V && !V->bIsNull ? Cast<UTexture>(ReadyAssets.FindRef(V->ObjectPath)) : nullptr;
		};
		UTexture* SourceTexture = Texture(From, TEXT("BlendBroker0"));
		UTexture* TargetStart = Texture(To, TEXT("SkyColor_Texture")); UTexture* TargetEnd = Texture(To, TEXT("BlendBroker0"));
		const bool bFrozenBlend = RecoveryTarget.SecondsPerHour == 0. && TargetStart != TargetEnd && RecoveryTarget.FrozenAlpha > 0.;
		if (bFrozenBlend && Beta >= 0.5f)
		{
			MID->SetTextureParameterValue(TEXT("SkyColor_Texture"), TargetStart); MID->SetTextureParameterValue(TEXT("BlendBroker0"), TargetEnd);
			MID->SetScalarParameterValue(TEXT("Blend0"), static_cast<float>((Beta - 0.5f) * 2. * RecoveryTarget.FrozenAlpha));
		}
		else
		{
			MID->SetTextureParameterValue(TEXT("SkyColor_Texture"), SourceTexture); MID->SetTextureParameterValue(TEXT("BlendBroker0"), TargetStart);
			MID->SetScalarParameterValue(TEXT("Blend0"), bFrozenBlend ? Beta * 2.f : Beta);
		}
	}
	auto* Directional = ResolveComponent<UDirectionalLightComponent>(ReadObject(TEXT("DirectionalLight")));
	auto* Sky = ResolveComponent<USkyLightComponent>(ReadObject(TEXT("SkyLight")));
	auto* Fog = ResolveComponent<UExponentialHeightFogComponent>(ReadObject(TEXT("ExponentialHeightFog")));
	auto* Billboard = ResolveComponent<UMeshComponent>(ReadObject(TEXT("SunBillboard")));
	Directional->SetLightColor(FMath::Lerp(A.DirectionalColor, B.DirectionalColor, Beta)); Sky->SetLightColor(FMath::Lerp(A.SkyLightColor, B.SkyLightColor, Beta));
	Fog->SetFogInscatteringColor(FMath::Lerp(A.FogColor, B.FogColor, Beta)); Fog->SetDirectionalInscatteringColor(FMath::Lerp(A.DirectionalFogColor, B.DirectionalFogColor, Beta));
	Directional->SetWorldRotation(FQuat::Slerp(A.DirectionalRotation, B.DirectionalRotation, Beta).GetNormalized());
	Billboard->SetWorldLocation(FMath::Lerp(A.BillboardLocation, B.BillboardLocation, Beta));
	if (A.BillboardMaterial == B.BillboardMaterial) Billboard->SetRelativeScale3D(FMath::Lerp(A.BillboardScale, B.BillboardScale, Beta));
	else
	{
		if (Beta >= 0.5f && Billboard->GetMaterial(0) != B.BillboardMaterial)
		{
			Billboard->SetRelativeScale3D(FVector::ZeroVector); Billboard->SetMaterial(0, B.BillboardMaterial);
		}
		Billboard->SetRelativeScale3D(Beta < 0.5f ? A.BillboardScale * (1. - Beta * 2.) : B.BillboardScale * ((Beta - 0.5) * 2.));
	}
	auto* Parameters = GetWorld()->GetParameterCollectionInstance(SkyParameters);
	Parameters->SetScalarParameterValue(TEXT("SunVisible"), FMath::Lerp(A.SunVisible, B.SunVisible, Beta)); Parameters->SetScalarParameterValue(TEXT("Darkness"), FMath::Lerp(A.Darkness, B.Darkness, Beta));
	const FVector SunDir = FQuat::Slerp(A.UnclampedSunRotation, B.UnclampedSunRotation, Beta).GetNormalized().GetForwardVector();
	Parameters->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 1.f));
	if (Beta >= 1.f)
	{
		FSWWeatherVisualFrame Endpoint; if (CaptureVisualFrame(Endpoint)) CompareVisualFrames(TEXT("RecoveryEndpointMismatch"), Endpoint, B);
		const FSWWeatherPlaybackSegment Target = RecoveryTarget;
		PlaybackBuffer.RemoveAll([&Target](const auto& S) { return S.Epoch != Target.Epoch || S.Sequence < Target.Sequence; });
		if (!PlaybackBuffer.ContainsByPredicate([&Target](const auto& S) { return SegmentKey(S) == SegmentKey(Target); })) PlaybackBuffer.Add(Target);
		if (RecoveryNext.Sequence && !PlaybackBuffer.ContainsByPredicate([this](const auto& S) { return SegmentKey(S) == SegmentKey(RecoveryNext); })) PlaybackBuffer.Add(RecoveryNext);
		PlaybackBuffer.Sort([](const auto& X, const auto& Y) { return SegmentKey(X) < SegmentKey(Y); });
		if (EnterSegment(Target))
		{
			PlaybackServerTime = Target.StartServerTime; LastPresentedAlpha = 0.;
			PlaybackStatus = Target.SecondsPerHour > 0. ? ESWWeatherPlaybackStatus::Playing : ESWWeatherPlaybackStatus::Frozen;
			EvaluatePresentedWeather();
			RecoveryTarget = {}; RecoveryNext = {}; RecoverySourceFrame = {}; RecoveryTargetFrame = {}; bHasVisualFrame = false;
			LogWeatherDiagnostics(TEXT("RecoveryComplete"), GetServerTime());
		}
	}
}

void ASWNetworkWeatherActor::CompareVisualFrames(const TCHAR* Phase, const FSWWeatherVisualFrame& From, const FSWWeatherVisualFrame& To)
{
	// TODO (2026-10-03): BoundaryDiscontinuity still occurs after the playback fixes.
	// Observed Epoch=1 Sequence=21, Hour 8->9: CloudMaterial.Star_GChannel delta=1.0.
	// The cause and visible impact remain unverified; commenting out diagnostics is not a fix.
	// Re-enable the UE_LOG calls below when investigating boundary continuity.
	if (!CVarWeatherDiagnostics.GetValueOnGameThread()) return;
	if (CVarWeatherDiagnostics.GetValueOnGameThread() < 2 && FCString::Strcmp(Phase, TEXT("FrameParameterChange")) == 0) return;
	float Maximum = 0.f; FString Parameter;
	auto Difference = [&Maximum, &Parameter](float Delta, const FString& Name) { if (Delta > Maximum) { Maximum = Delta; Parameter = Name; } };
	auto ColorDifference = [&Difference](const FLinearColor& A, const FLinearColor& B, const FString& Name)
	{
		Difference(FMath::Max(FMath::Max(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G)), FMath::Max(FMath::Abs(A.B - B.B), FMath::Abs(A.A - B.A))), Name);
	};
	ColorDifference(From.DirectionalColor, To.DirectionalColor, TEXT("DirectionalLight")); ColorDifference(From.SkyLightColor, To.SkyLightColor, TEXT("SkyLight"));
	ColorDifference(From.FogColor, To.FogColor, TEXT("Fog")); ColorDifference(From.DirectionalFogColor, To.DirectionalFogColor, TEXT("DirectionalFog"));
	Difference(FMath::Abs(From.SunVisible - To.SunVisible), TEXT("SunVisible")); Difference(FMath::Abs(From.Darkness - To.Darkness), TEXT("Darkness"));
	FString SkyDetails;
	for (const auto& M : From.Materials)
	{
		const auto* N = To.Materials.FindByPredicate([&M](const auto& V) { return V.PropertyName == M.PropertyName; }); if (!N) continue;
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(ReadObject(M.PropertyName)); if (!MID) continue;
		TSet<FName> Scalars, Vectors;
		for (const auto& V : M.Scalars) Scalars.Add(V.Name); for (const auto& V : N->Scalars) Scalars.Add(V.Name);
		for (const auto& V : M.Vectors) Vectors.Add(V.Name); for (const auto& V : N->Vectors) Vectors.Add(V.Name);
		for (FName Name : Scalars)
		{
			if (Name == TEXT("Blend0") && FCString::Strcmp(Phase, TEXT("FrameParameterChange")) != 0) continue;
			auto Value = [Name, MID](const auto& State, float& Result)
			{
				if (const auto* V = State.Scalars.FindByPredicate([Name](const auto& P) { return P.Name == Name; })) { Result = V->Value; return true; }
				return MID->Parent->GetScalarParameterValue(FMaterialParameterInfo(Name), Result);
			};
			float A = 0.f, B = 0.f;
			if (Value(M, A) && Value(*N, B)) Difference(FMath::Abs(A - B), M.PropertyName.ToString() + TEXT(".") + Name.ToString());
		}
		for (FName Name : Vectors)
		{
			auto Value = [Name, MID](const auto& State, FLinearColor& Result)
			{
				if (const auto* V = State.Vectors.FindByPredicate([Name](const auto& P) { return P.Name == Name; })) { Result = V->Value; return true; }
				return MID->Parent->GetVectorParameterValue(FMaterialParameterInfo(Name), Result);
			};
			FLinearColor A, B; if (Value(M, A) && Value(*N, B)) ColorDifference(A, B, M.PropertyName.ToString() + TEXT(".") + Name.ToString());
		}
		auto SkyTexture = [](const auto& State, FName Name)
		{
			const auto* V = State.Textures.FindByPredicate([Name](const auto& P) { return P.Name == Name; }); return V ? V->ObjectPath : FString();
		};
		SkyDetails += FString::Printf(TEXT(" %s Sky=%s->%s Target=%s->%s"), *M.PropertyName.ToString(), *SkyTexture(M, TEXT("SkyColor_Texture")), *SkyTexture(*N, TEXT("SkyColor_Texture")), *SkyTexture(M, TEXT("BlendBroker0")), *SkyTexture(*N, TEXT("BlendBroker0")));
		if (M.PropertyName != TEXT("CurrentDynamicParam") && FCString::Strcmp(Phase, TEXT("BoundaryDiscontinuity")) == 0
			&& SkyTexture(M, TEXT("BlendBroker0")) != SkyTexture(*N, TEXT("SkyColor_Texture"))) Difference(1.f, M.PropertyName.ToString() + TEXT(".SkyEndpoint"));
	}
	if (Maximum > 0.01f)
	{
		if (CVarWeatherDiagnostics.GetValueOnGameThread() < 2)
		{
			// UE_LOG(LogSWWeatherDiagnostics, Warning, TEXT("%s Epoch=%u Sequence=%u Max=%.6f Parameter=%s Alpha=%.6f Step=%.6f"), Phase, AppliedEpoch, AppliedSequence, Maximum, *Parameter, LastPresentedAlpha, PlaybackStep);
			return;
		}
		FString Curves;
		for (const auto& P : ActiveWeatherState.Properties) for (const auto& R : P.References)
			if (!R.bIsNull && R.Kind == ESWWeatherAssetKind::ScalarCurve) Curves += TEXT(" ") + R.MemberPath + TEXT("=") + R.ObjectPath;
		// UE_LOG(LogSWWeatherDiagnostics, Warning, TEXT("%s Epoch=%u Sequence=%u Max=%.6f Parameter=%s Alpha=%.6f Step=%.6f Curve=%s SunCurve=%s AuthoredScalarCurves=%s%s"),
		// 	Phase, AppliedEpoch, AppliedSequence, Maximum, *Parameter, LastPresentedAlpha, PlaybackStep, *GetPathNameSafe(ReadObject(TEXT("CurrentOverallColor"))), *GetPathNameSafe(ReadObject(TEXT("SunAngle"))), *Curves, *SkyDetails);
	}
}

void ASWNetworkWeatherActor::LogOnce(uint32 Epoch, uint32 Sequence, const FString& Reason)
{
	const FString Key = FString::Printf(TEXT("%u/%u/%s"), Epoch, Sequence, *Reason);
	if (ReportedErrors.Contains(Key)) return;
	ReportedErrors.Add(Key);
	// UE_LOG(LogSWWeatherDiagnostics, Warning, TEXT("Epoch=%u Sequence=%u Reason=%s"), Epoch, Sequence, *Reason);
}

void ASWNetworkWeatherActor::LogWeatherDiagnostics(const TCHAR* Phase, double ServerTime) const
{
	if (!CVarWeatherDiagnostics.GetValueOnGameThread()) return;
	const uint32 Oldest = PlaybackBuffer.IsEmpty() ? 0 : PlaybackBuffer[0].Sequence, Newest = PlaybackBuffer.IsEmpty() ? 0 : PlaybackBuffer.Last().Sequence;
	double Ahead = 0.;
	for (const auto& S : PlaybackBuffer) if (S.Epoch == AppliedEpoch && IsReady(S)) Ahead = FMath::Max(Ahead, S.StartServerTime + S.SecondsPerHour - PlaybackServerTime);
	if (CVarWeatherDiagnostics.GetValueOnGameThread() < 2)
	{
		// UE_LOG(LogSWWeatherDiagnostics, Log, TEXT("Phase=%s Epoch=%u Sequence=%u Hour=%d Alpha=%.6f Playback=%.6f Error=%.6f Rate=%.6f Status=%d BufferAhead=%.3f BufferCount=%d"), Phase, AppliedEpoch, AppliedSequence, ActiveWeatherState.Hour, LastPresentedAlpha, PlaybackServerTime, ServerTime - 2. - PlaybackServerTime, PlaybackRate, static_cast<int32>(PlaybackStatus), Ahead, PlaybackBuffer.Num());
		return;
	}
	FString Details;
	for (FName Name : WeatherMaterials()) if (auto* M = Cast<UMaterialInstanceDynamic>(ReadObject(Name)))
		Details += FString::Printf(TEXT(" %s{Blend0=%.4f Sky=%s Target=%s}"), *Name.ToString(), M->K2_GetScalarParameterValue(TEXT("Blend0")), *GetPathNameSafe(M->K2_GetTextureParameterValue(TEXT("SkyColor_Texture"))), *GetPathNameSafe(M->K2_GetTextureParameterValue(TEXT("BlendBroker0"))));
	if (auto* Light = ResolveComponent<UDirectionalLightComponent>(ReadObject(TEXT("DirectionalLight")))) Details += TEXT(" DirectionalRGB=") + Light->GetLightColor().ToString();
	if (auto* Light = ResolveComponent<USkyLightComponent>(ReadObject(TEXT("SkyLight")))) Details += TEXT(" SkyLightRGB=") + Light->GetLightColor().ToString();
	if (auto* Fog = ResolveComponent<UExponentialHeightFogComponent>(ReadObject(TEXT("ExponentialHeightFog")))) Details += TEXT(" FogRGB=") + Fog->FogInscatteringLuminance.ToString() + TEXT(" DirectionalFogRGB=") + Fog->DirectionalInscatteringLuminance.ToString();
	float Visible = -1.f, Darkness = -1.f;
	if (auto* P = GetWorld()->GetParameterCollectionInstance(SkyParameters)) { P->GetScalarParameterValue(TEXT("SunVisible"), Visible); P->GetScalarParameterValue(TEXT("Darkness"), Darkness); }
	// UE_LOG(LogSWWeatherDiagnostics, Log, TEXT("Phase=%s World=%s Authority=%d NetMode=%d Epoch=%u Sequence=%u ReceivedEpoch=%u Oldest=%u Newest=%u LocalElapsed=%.6f Step=%.6f Server=%.6f Target=%.6f Playback=%.6f Error=%.6f Rate=%.6f Status=%d BufferAhead=%.6f BufferCount=%d Hour=%d Alpha=%.6f Visible=%.4f Darkness=%.4f%s"),
	// 	Phase, *GetWorld()->GetName(), HasAuthority(), static_cast<int32>(GetNetMode()), AppliedEpoch, AppliedSequence, ReceivedEpoch, Oldest, Newest,
	// 	LocalElapsed, PlaybackStep, ServerTime, ServerTime - 2., PlaybackServerTime, ServerTime - 2. - PlaybackServerTime, PlaybackRate, static_cast<int32>(PlaybackStatus), Ahead, PlaybackBuffer.Num(), ActiveWeatherState.Hour, LastPresentedAlpha, Visible, Darkness, *Details);
}

void ASWNetworkWeatherActor::SampleWeatherDiagnostics(double ServerTime, double Alpha)
{
	const double LocalTime = GetWorld()->GetTimeSeconds();
	if (DiagnosticLastSequence)
	{
		const double Correction = (ServerTime - DiagnosticLastServerTime) - (LocalTime - DiagnosticLastLocalTime);
		if (FMath::Abs(Correction) > 0.05) LogWeatherDiagnostics(TEXT("ClockJump"), ServerTime);
		if (AppliedSequence == DiagnosticLastSequence && Alpha < DiagnosticLastAlpha - 1.e-6) LogWeatherDiagnostics(TEXT("AlphaRegression"), ServerTime);
	}
	if (AppliedEpoch && FMath::Abs(ServerTime - 2. - PlaybackServerTime) > 0.1 && LocalTime >= NextClockErrorTime)
	{
		LogWeatherDiagnostics(TEXT("ClockError"), ServerTime); NextClockErrorTime = LocalTime + 5.;
	}
	if (LocalTime >= DiagnosticNextSampleTime) { LogWeatherDiagnostics(TEXT("Sample"), ServerTime); DiagnosticNextSampleTime = LocalTime + (CVarWeatherDiagnostics.GetValueOnGameThread() >= 2 ? 1. : 5.); }
	DiagnosticLastSequence = AppliedSequence; DiagnosticLastAlpha = Alpha; DiagnosticLastLocalTime = LocalTime; DiagnosticLastServerTime = ServerTime;
}

void ASWNetworkWeatherActor::PruneReadyAssets()
{
	TSet<FString> Used;
	auto AddSegment = [&Used](const FSWWeatherPlaybackSegment& S)
	{
		for (const auto& P : S.Properties) for (const auto& R : P.References) if (!R.bIsNull) Used.Add(R.ObjectPath);
		for (const auto& M : S.Materials) for (const auto& T : M.Textures) if (!T.bIsNull) Used.Add(T.ObjectPath);
	};
	AddSegment(ActiveWeatherState); AddSegment(PlannerState); AddSegment(RecoveryTarget); AddSegment(RecoveryNext);
	for (const auto& S : PlaybackBuffer) AddSegment(S); for (const auto& S : PendingEpochBuffer) AddSegment(S); for (const auto& S : ServerHistory) AddSegment(S);
	for (const auto* Frame : {&RecoverySourceFrame, &RecoveryTargetFrame, &LastVisualFrame}) for (const auto& M : Frame->Materials) for (const auto& T : M.Textures) if (!T.bIsNull) Used.Add(T.ObjectPath);
	for (const auto& Pair : AssetLoadHandles) Used.Add(Pair.Key);
	for (auto It = ReadyAssets.CreateIterator(); It; ++It) if (!Used.Contains(It.Key())) It.RemoveCurrent();
	TSet<uint64> IDs;
	for (const auto& S : PlaybackBuffer) IDs.Add(SegmentKey(S)); for (const auto& S : PendingEpochBuffer) IDs.Add(SegmentKey(S));
	IDs.Add(SegmentKey(ActiveWeatherState)); IDs.Add(SegmentKey(RecoveryTarget)); IDs.Add(SegmentKey(RecoveryNext));
	for (auto It = SegmentReadiness.CreateIterator(); It; ++It) if (!IDs.Contains(It.Key())) It.RemoveCurrent();
}

void ASWNetworkWeatherActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
	const USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	const double LocalTime = GetWorld()->GetTimeSeconds();
	LocalElapsed = FMath::Max(0., LocalTime - LastLocalWorldTime);
	if (LocalTime < LastLocalWorldTime) { LogWeatherDiagnostics(TEXT("LocalClockReset"), GetServerTime()); ClockSamples.Reset(); ClockSampleStartTime = LocalTime; NextClockSampleTime = LocalTime; }
	LastLocalWorldTime = LocalTime; PlaybackStep = 0.;
	if (!bAdapterReady || (!HasAuthority() && !GetWorld()->GetGameState())) return;
	const double Now = GetServerTime();
	if (!FMath::IsFinite(Now)) return;
	if (HasAuthority()) { BuildFutureSegments(Now); PublishPlaybackWindow(Now); }
	else if (!PlaybackWindow.Segments.IsEmpty()) MergePlaybackWindow(PlaybackWindow);
	if (GetNetMode() != NM_DedicatedServer)
	{
		PrepareSegments();
		bool bStarted = false, bRecoveryTick = false;
		if (!AppliedEpoch) bStarted = TryStartPlayback(Now, false);
		else if (ReceivedEpoch > AppliedEpoch) bStarted = TryStartPlayback(Now, true);
		else if (PlaybackStatus == ESWWeatherPlaybackStatus::WaitingRecoveryTarget)
		{
			if (TryStartRecovery(Now)) { bRecoveryTick = true; AdvanceRecovery(0.); }
		}
		else if (PlaybackStatus == ESWWeatherPlaybackStatus::Recovering)
		{
			bRecoveryTick = true; PlaybackStep = FMath::Min(LocalElapsed, 0.250); AdvanceRecovery(PlaybackStep);
		}
		else if (AppliedEpoch && PlaybackStatus != ESWWeatherPlaybackStatus::Frozen)
		{
			const double Error = Now - 2. - PlaybackServerTime;
			const double DesiredRate = FMath::Abs(Error) <= 0.020 ? 1. : FMath::Clamp(1. + 0.1 * Error, 0.98, 1.02);
			const double BoundedElapsed = FMath::Min(LocalElapsed, 0.250);
			PlaybackRate += FMath::Clamp(DesiredRate - PlaybackRate, -0.1 * BoundedElapsed, 0.1 * BoundedElapsed);
			PlaybackStep = BoundedElapsed * PlaybackRate;
			AdvanceAcrossSegments(PlaybackStep);
		}
		if (AppliedEpoch && !bRecoveryTick && PlaybackStatus != ESWWeatherPlaybackStatus::WaitingEpochReset)
		{
			EvaluatePresentedWeather();
			FSWWeatherVisualFrame Current;
			if (CaptureVisualFrame(Current))
			{
				if (bHasVisualFrame && !bStarted && DiagnosticLastSequence == AppliedSequence) CompareVisualFrames(TEXT("FrameParameterChange"), LastVisualFrame, Current);
				LastVisualFrame = MoveTemp(Current); bHasVisualFrame = true;
			}
		}
	}
	AdvanceWind(Now);
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (WindState.bInitialized && (!Voyage || !Voyage->IsActiveVoyageSession() || WindState.Generation == Voyage->GetGeneration())) if (auto* Parameters = GetWorld()->GetParameterCollectionInstance(SkyParameters))
		{
			const FVector Offset = ResolveWindOffset(Now); Parameters->SetVectorParameterValue(TEXT("WindOffset"), FLinearColor(Offset.X, Offset.Y, Offset.Z, 1.f));
		}
		SampleWeatherDiagnostics(Now, LastPresentedAlpha);
	}
	PruneReadyAssets();
}

FName ASWNetworkWeatherActor::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<ASWNetworkWeatherActor*>(this)) : NAME_None;
}

ESWVoyageStepResult ASWNetworkWeatherActor::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!bAdapterReady)
	{
		OutError = TEXT("WeatherAdapterInvalid");
		return ESWVoyageStepResult::Failed;
	}
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWNetworkWeatherActor::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	for (auto& Pair : AssetLoadHandles) if (Pair.Value) Pair.Value->CancelHandle();
	AssetLoadHandles.Reset(); CompletedAssetLoads.Reset(); FailedAssetPaths.Reset(); SegmentReadiness.Reset(); ClockSamples.Reset();
	PlaybackBuffer.Reset(); PendingEpochBuffer.Reset(); ServerHistory.Reset();
	ActiveWeatherState = {}; RecoveryTarget = {}; RecoveryNext = {}; PlaybackWindow = {};
	RecoverySourceFrame = {}; RecoveryTargetFrame = {}; LastVisualFrame = {};
	AppliedEpoch = AppliedSequence = ReceivedEpoch = 0; ReceivedPublishedTime = EpochPlaybackStartServerTime = 0.;
	PlaybackServerTime = LocalElapsed = PlaybackStep = RecoveryElapsed = LastPresentedAlpha = 0.; PlaybackRate = 1.;
	LastLocalWorldTime = GetWorld()->GetTimeSeconds(); NextClockSampleTime = 0.;
	bGenerationFailed = bInitialWindowPublished = bHasVisualFrame = false;
	RestoredVoyageGeneration = INDEX_NONE;
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult ASWNetworkWeatherActor::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	const USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation) || !bAdapterReady)
	{
		OutError = TEXT("WeatherAdapterOrVoyageGenerationInvalid");
		return ESWVoyageStepResult::Failed;
	}
	if (RestoredVoyageGeneration != Context.Generation)
	{
		if (!Context.bAuthority)
		{
			const FSWWeatherPlaybackWindow ReceivedWindow = PlaybackWindow;
			ResetVoyageTransientState_Implementation(Context, OutError);
			if (ReceivedWindow.Generation == Context.Generation) PlaybackWindow = ReceivedWindow;
		}
		// Continue retains the pre-existing cold-load weather policy: authored
		// initial settings, without extending the room snapshot format.
		if (Context.bAuthority && (!Context.bContinue || (Context.bBootstrap && ServerHistory.IsEmpty())))
		{
			if (!bHasInitialVoyageSettings || PlannerState.Epoch == MAX_uint32)
			{
				OutError = TEXT("WeatherInitialSettingsOrEpochInvalid");
				return ESWVoyageStepResult::Failed;
			}
			const uint32 PreviousEpoch = PlannerState.Epoch;
			TGuardValue<bool> InitializeScope(bInitializingVoyageWeather, true);
			WriteNumber(TEXT("WeatherChangeCycle"), FMath::Max(1., ReadNumber(TEXT("WeatherChangeCycle"))));
			if (!Invoke(TEXT("WeatherRaffleSetting")))
			{
				OutError = TEXT("WeatherRaffleSettingFailed");
				return ESWVoyageStepResult::Failed;
			}
			SetNetworkWeatherTime(InitialVoyageHour, InitialVoyageMinute, InitialVoyageWeather);
			if (PlannerState.Epoch != PreviousEpoch + 1 || bGenerationFailed)
			{
				OutError = TEXT("WeatherInitialPublicationFailed");
				return ESWVoyageStepResult::Failed;
			}
			WindState = InitialVoyageWind; WindState.Generation = Context.Generation;
			WindState.StartServerTime = GetServerTime(); NextWindServerTime = WindState.StartServerTime;
			ForceNetUpdate();
		}
		RestoredVoyageGeneration = Context.Generation;
	}
	if (FutureVoyageWindow.Generation == Context.Generation && !FutureVoyageWindow.Segments.IsEmpty())
	{
		MergePlaybackWindow(FutureVoyageWindow); FutureVoyageWindow = {};
	}
	if (!PlaybackWindow.Segments.IsEmpty()) MergePlaybackWindow(PlaybackWindow);
	if (GetNetMode() != NM_DedicatedServer)
	{
		PrepareSegments();
		if (!AppliedEpoch) TryStartPlayback(GetServerTime(), false);
		if (AppliedEpoch)
		{
			EvaluatePresentedWeather(); bHasVisualFrame = CaptureVisualFrame(LastVisualFrame);
		}
	}
	return IsVoyageReady_Implementation(Context, OutError);
}

ESWVoyageStepResult ASWNetworkWeatherActor::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (bGenerationFailed || !bAdapterReady)
	{
		OutError = TEXT("WeatherGenerationOrAdapterFailed");
		return ESWVoyageStepResult::Failed;
	}
	for (const auto& Pair : SegmentReadiness)
		if (Pair.Value == ESWWeatherSegmentReadiness::Invalid)
		{
			OutError = TEXT("WeatherRequiredSegmentInvalid");
			return ESWVoyageStepResult::Failed;
		}
	if (Context.bAuthority && (!bInitialWindowPublished || PlaybackWindow.Generation != Context.Generation)) return ESWVoyageStepResult::Pending;
	if (!WindState.bInitialized || WindState.Generation != Context.Generation) return ESWVoyageStepResult::Pending;
	if (GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) return ESWVoyageStepResult::Succeeded;
	return AppliedEpoch != 0 && bHasVisualFrame ? ESWVoyageStepResult::Succeeded : ESWVoyageStepResult::Pending;
}
