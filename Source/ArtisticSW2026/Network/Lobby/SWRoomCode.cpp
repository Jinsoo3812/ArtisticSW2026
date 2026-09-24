#include "Network/Lobby/SWRoomCode.h"

namespace
{
constexpr TCHAR Alphabet[] = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
}

bool FSWRoomCode::ParsePublicIPv4(const FString& Address, uint8 OutOctets[4])
{
	TArray<FString> Parts;
	Address.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 4) return false;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Parts[Index].IsEmpty() || Parts[Index].Len() > 3) return false;
		int32 Value = 0;
		for (TCHAR Character : Parts[Index])
		{
			if (Character < TEXT('0') || Character > TEXT('9')) return false;
			Value = Value * 10 + Character - TEXT('0');
		}
		if (Value > 255) return false;
		OutOctets[Index] = static_cast<uint8>(Value);
	}
	const uint8 A = OutOctets[0], B = OutOctets[1], C = OutOctets[2];
	return !(A == 0 || A == 10 || A == 127 || A >= 224
		|| (A == 100 && B >= 64 && B <= 127)
		|| (A == 169 && B == 254)
		|| (A == 172 && B >= 16 && B <= 31)
		|| (A == 192 && (B == 168 || (B == 0 && C == 2)))
		|| (A == 198 && (B == 18 || B == 19 || (B == 51 && C == 100)))
		|| (A == 203 && B == 0 && C == 113));
}

uint16 FSWRoomCode::Crc16(const uint8* Bytes, int32 Count)
{
	uint16 Crc = 0xffff;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Crc ^= static_cast<uint16>(Bytes[Index]) << 8;
		for (int32 Bit = 0; Bit < 8; ++Bit)
			Crc = (Crc & 0x8000) ? static_cast<uint16>((Crc << 1) ^ 0x1021) : static_cast<uint16>(Crc << 1);
	}
	return Crc;
}

bool FSWRoomCode::Encode(const FString& Address, uint16 Port, FString& OutCode)
{
	uint8 Bytes[9] = {1};
	if (!Port || !ParsePublicIPv4(Address, Bytes + 1)) return false;
	Bytes[5] = static_cast<uint8>(Port >> 8);
	Bytes[6] = static_cast<uint8>(Port);
	const uint16 Crc = Crc16(Bytes, 7);
	Bytes[7] = static_cast<uint8>(Crc >> 8);
	Bytes[8] = static_cast<uint8>(Crc);
	FString Raw;
	uint32 Buffer = 0;
	int32 Bits = 0;
	for (uint8 Byte : Bytes)
	{
		Buffer = (Buffer << 8) | Byte;
		Bits += 8;
		while (Bits >= 5)
		{
			Bits -= 5;
			Raw.AppendChar(Alphabet[(Buffer >> Bits) & 31]);
		}
	}
	if (Bits) Raw.AppendChar(Alphabet[(Buffer << (5 - Bits)) & 31]);
	OutCode = Raw.Left(5) + TEXT("-") + Raw.Mid(5, 5) + TEXT("-") + Raw.Mid(10);
	return true;
}

bool FSWRoomCode::Decode(const FString& Code, FString& OutAddress, uint16& OutPort)
{
	FString Raw;
	for (TCHAR Character : Code)
	{
		if (Character == TEXT(' ') || Character == TEXT('-')) continue;
		Character = FChar::ToUpper(Character);
		if (!((Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('2') && Character <= TEXT('7')))) return false;
		Raw.AppendChar(Character);
	}
	if (Raw.Len() != 15) return false;
	uint8 Bytes[9] = {};
	uint32 Buffer = 0;
	int32 Bits = 0, ByteIndex = 0;
	for (TCHAR Character : Raw)
	{
		const int32 Value = Character <= TEXT('Z') ? Character - TEXT('A') : Character - TEXT('2') + 26;
		Buffer = (Buffer << 5) | Value;
		Bits += 5;
		if (Bits >= 8)
		{
			Bits -= 8;
			if (ByteIndex >= 9) return false;
			Bytes[ByteIndex++] = static_cast<uint8>((Buffer >> Bits) & 255);
		}
	}
	if (ByteIndex != 9 || Bits != 3 || (Buffer & 7) != 0 || Bytes[0] != 1) return false;
	if (Crc16(Bytes, 7) != (static_cast<uint16>(Bytes[7]) << 8 | Bytes[8])) return false;
	const uint16 Port = static_cast<uint16>(Bytes[5]) << 8 | Bytes[6];
	const FString Address = FString::Printf(TEXT("%u.%u.%u.%u"), Bytes[1], Bytes[2], Bytes[3], Bytes[4]);
	uint8 Parsed[4];
	if (!Port || !ParsePublicIPv4(Address, Parsed)) return false;
	OutAddress = Address;
	OutPort = Port;
	return true;
}
