#pragma once

#include "CoreMinimal.h"

/** Version-one public IPv4 room address codec. No secret or authentication is implied. */
class ARTISTICSW2026_API FSWRoomCode
{
public:
	static bool ParsePublicIPv4(const FString& Address, uint8 OutOctets[4]);
	static bool Encode(const FString& Address, uint16 Port, FString& OutCode);
	static bool Decode(const FString& Code, FString& OutAddress, uint16& OutPort);
	static uint16 Crc16(const uint8* Bytes, int32 Count);
};
