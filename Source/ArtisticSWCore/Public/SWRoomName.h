#pragma once

#include "CoreMinimal.h"

class ARTISTICSWCORE_API FSWRoomName
{
public:
	static bool Normalize(const FString& Input, FString& OutName);
	static FString ToHex(const FString& Name);
	static bool FromHex(const FString& Hex, FString& OutName);
};
