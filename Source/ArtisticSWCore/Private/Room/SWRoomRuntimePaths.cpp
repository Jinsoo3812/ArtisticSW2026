#include "Room/SWRoomRuntimePaths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

bool FSWRoomRuntimePaths::TryResolveRoot(FString& OutRoot, FString& OutError)
{
	OutRoot.Reset();
	OutError.Reset();
	FString Value;
	const bool bParsed = FParse::Value(FCommandLine::Get(), TEXT("SWRoomRoot="), Value);
	const bool bExplicit = bParsed || FString(FCommandLine::Get()).Contains(TEXT("-SWRoomRoot="), ESearchCase::IgnoreCase);
	Value = bExplicit ? Value.TrimStartAndEnd() : FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	const bool bDriveAbsolute = Value.Len() >= 3 && FChar::IsAlpha(Value[0]) && Value[1] == ':'
		&& (Value[2] == '/' || Value[2] == '\\');
	if (!bDriveAbsolute || FPaths::IsRelative(Value) || Value.Contains(TEXT("\""))
		|| Value.Contains(TEXT("\r")) || Value.Contains(TEXT("\n")))
	{
		OutError = TEXT("방 저장 경로가 올바르지 않습니다. SWRoomRoot에 로컬 절대 폴더 경로를 지정하세요.");
		return false;
	}
	FPaths::NormalizeDirectoryName(Value);
	if (!FPaths::CollapseRelativeDirectories(Value) || Value.Len() <= 3)
	{
		OutError = TEXT("방 저장 경로가 올바르지 않습니다. SWRoomRoot에 로컬 절대 폴더 경로를 지정하세요.");
		return false;
	}
	OutRoot = Value;
	return true;
}

FString FSWRoomRuntimePaths::GetRoot()
{
	FString Root, Error;
	return TryResolveRoot(Root, Error) ? Root : FString();
}

FString FSWRoomRuntimePaths::GetSaveDirectory()
{
	const FString Root = GetRoot();
	return Root.IsEmpty() ? FString() : FPaths::Combine(Root, TEXT("SWRoom"));
}

FString FSWRoomRuntimePaths::GetHostDirectory()
{
	const FString Root = GetRoot();
	return Root.IsEmpty() ? FString() : FPaths::Combine(Root, TEXT("RoomHost"));
}
