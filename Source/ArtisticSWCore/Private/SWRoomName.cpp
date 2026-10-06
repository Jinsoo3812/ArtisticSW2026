#include "SWRoomName.h"
#include "Containers/StringConv.h"

bool FSWRoomName::Normalize(const FString& Input, FString& OutName)
{
	const FString Trimmed = Input.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Trimmed.Len() > 16) return false;
	for (TCHAR Character : Trimmed)
	{
		if (Character < 0x20 || Character == 0x7f) return false;
	}
	FTCHARToUTF8 Utf8(*Trimmed);
	if (Utf8.Length() > 48) return false;
	OutName = Trimmed;
	return true;
}

FString FSWRoomName::ToHex(const FString& Name)
{
	FTCHARToUTF8 Utf8(*Name);
	return BytesToHex(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

bool FSWRoomName::FromHex(const FString& Hex, FString& OutName)
{
	if (Hex.IsEmpty() || Hex.Len() > 96 || Hex.Len() % 2) return false;
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Hex.Len() / 2);
	for (int32 Index = 0; Index < Hex.Len(); Index += 2)
	{
		const int32 Hi = FParse::HexDigit(Hex[Index]);
		const int32 Lo = FParse::HexDigit(Hex[Index + 1]);
		if (Hi < 0 || Lo < 0) return false;
		Bytes[Index / 2] = static_cast<uint8>((Hi << 4) | Lo);
	}
	Bytes.Add(0);
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num() - 1);
	const FString Decoded(Converted.Length(), Converted.Get());
	if (!ToHex(Decoded).Equals(Hex, ESearchCase::IgnoreCase)) return false;
	FString Normalized;
	if (!Normalize(Decoded, Normalized) || Normalized != Decoded) return false;
	OutName = MoveTemp(Normalized);
	return true;
}
