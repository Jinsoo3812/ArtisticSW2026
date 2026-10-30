# 직접 서버 빌드·패키징하기

현재 PC의 UE 5.7.4 소스 엔진과 MSVC 14.44.35228로 실행한다. GitHub 인증·엔진 소스 다운로드·Setup·컴파일러 설치를 매번 반복하지 않는다.

## 일반 사용: 서버 패키지 만들기

1. 프로젝트를 저장하고 이 프로젝트의 Unreal Editor를 종료한다. 빌드 중 코드·콘텐츠를 편집하거나 다른 빌드를 동시에 실행하지 않는다.
2. `Scripts/BuildServer.bat`을 더블클릭한다. 관리자 권한은 보통 필요하지 않다.
3. 종료 후 콘솔의 `SUCCESS: Server package:` 경로를 확인한다. 최종 종료 코드가 0이면 패키징과 Launcher 에디터 복구 빌드 모두 성공한 것이다.

PowerShell에서 실행해도 된다.

```powershell
cd 'C:\Unreal Projects\ArtisticSW2026'
.\Scripts\BuildServer.bat
```

서버 C++ 빌드, 서버용 Cook, Stage/Package/Archive를 수행한다. 각 실행은 아래처럼 별도 결과 폴더를 사용하므로 이전 성공 패키지를 덮어쓰지 않는다.

```text
C:\UnrealBuilds\ArtisticSW2026Server-UE574\<날짜_시간_실행번호>\WindowsServer
```

`WindowsServer` 폴더 전체가 배포 단위다. 루트 exe만 복사하지 않는다. Development 구성으로 개발용 심볼도 포함될 수 있다. 서버 실행 파일이 만들어져도 실제 실행·접속 검증은 별도다.

## 빠른 사용: 컴파일만 하기

```powershell
.\Scripts\BuildServer.bat build
```

서버 C++ 컴파일만 수행한다. Cook·패키징을 생략하므로 맵·Blueprint·콘텐츠 변경을 배포 결과에 반영하지 않는다. 배포하거나 패키징된 서버를 갱신할 때는 기본 모드를 사용한다.

설정 경로만 검사하려면 다음을 사용한다. 실제 컴파일러 실행이나 빌드는 하지 않는다.

```powershell
.\Scripts\BuildServer.bat check
```

## 로그와 실패 처리

실행별 로그는 프로젝트의 `Saved/Logs/ServerBuilds/<실행별 폴더>`에 저장한다.

- `package.log`: 서버·소스 에디터·패키징 도구 빌드, Cook·패키징 결과.
- `launcher-editor.log`: 기존 Launcher 엔진용 에디터 모듈 복구 빌드.
- `server-build.log`: 컴파일 전용 모드의 결과.

패키징 성공과 Launcher 복구 실패는 콘솔에 별도로 표시한다. 실패 시 해당 로그의 첫 오류부터 확인한다. 스크립트는 소스 코드를 자동 수정하지 않는다.

소스 엔진으로 패키징하면 프로젝트의 에디터 DLL도 소스 엔진 기준으로 생성된다. 따라서 기본 모드는 패키징 성공·실패 후 모두 Launcher Development Editor 빌드를 수행하여 기존 개발 환경에 맞춰 복구한다. `.uproject`의 EngineAssociation과 방 생성 로직은 바꾸지 않는다.

Ctrl+C 또는 창 닫기로 중단하면 마지막 복구 단계가 실행되지 않을 수 있다. 이때 Launcher 에디터 모듈만 복구하려면 다음 명령을 사용한다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' ArtisticSW2026Editor Win64 Development '-Project=C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject' -WaitMutex -NoHotReload -2026 '-CompilerVersion=14.44.35228'
```

## 경로와 컴파일러 설정

다른 PC나 엔진 경로를 사용할 때는 `BuildServer.bat` 상단의 SourceEngine, LauncherEngine, ArchiveBase를 수정한다. 프로젝트 경로는 스크립트 위치에서 계산한다.

이번에 성공한 소스 엔진 설정 파일은 다음 위치다.

```text
C:\UnrealSource\UE5.7.4\Engine\Saved\UnrealBuildTool\BuildConfiguration.xml
```

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
    <WindowsPlatform>
        <Compiler>VisualStudio2026</Compiler>
        <CompilerVersion>14.44.35228</CompilerVersion>
    </WindowsPlatform>
</Configuration>
```

MSVC 14.44 구성 요소가 설치되어 있어야 한다. 이 XML은 별도 소스 엔진에만 적용하며, 스크립트는 설치나 설정 덮어쓰기를 자동 실행하지 않는다. 엔진 버전을 변경하면 스크립트와 도구 체인 설정도 함께 검토한다.

UBT는 변경된 코드와 의존성을 증분 빌드한다. 이 스크립트의 Cook은 반복 Cook 시간을 줄이는 옵션을 별도로 사용하지 않으므로 콘텐츠 규모에 따라 시간이 걸린다. 방 생성의 기본값은 패키징 서버(`sw.Room.ServerBackend 1`)이며, 실행 경로는 아래와 같다. 기존 패키지에는 코드 변경이 자동 반영되지 않으므로 클라이언트와 서버를 다시 패키징해야 한다.

## 전체 PowerShell 명령: 서버 빌드부터 패키징·에디터 복구까지

아래는 BuildServer.bat package와 같은 주요 빌드 단계를 직접 실행하는 명령이다. 빌드 전 에디터를 닫는다. PowerShell 창을 강제 종료하면 finally 복구가 실행되지 않을 수 있다. Ctrl+C로 중단하지 말고 정상 종료까지 기다린다.

```powershell
$project = 'C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject'
$sourceEngine = 'C:\UnrealSource\UE5.7.4'
$launcherEngine = 'C:\Program Files\Epic Games\UE_5.7'
$archive = 'C:\UnrealBuilds\ArtisticSW2026Server-UE574\' + (Get-Date -Format 'yyyyMMdd_HHmmss')
$packageExit = 1
$restoreExit = 1
$uatArgs = @(
    "-ScriptsForProject=$project", 'BuildCookRun', "-project=$project",
    "-unrealexe=$sourceEngine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    '-noP4', '-unattended', '-utf8output',
    '-server', '-noclient', '-serverplatform=Win64', '-serverconfig=Development',
    '-build', '-cook', '-stage', '-pak', '-iostore', '-package', '-archive',
    "-archivedirectory=$archive"
)
try {
    & "$sourceEngine\Engine\Build\BatchFiles\RunUAT.bat" @uatArgs
    $packageExit = $LASTEXITCODE
}
finally {
    & "$launcherEngine\Engine\Build\BatchFiles\Build.bat" ArtisticSW2026Editor Win64 Development "-Project=$project" -WaitMutex -NoHotReload -2026 '-CompilerVersion=14.44.35228'
    $restoreExit = $LASTEXITCODE
}
if ($packageExit -ne 0 -or $restoreExit -ne 0) {
    throw "서버 패키징=$packageExit, Launcher 복구=$restoreExit. 로그의 첫 오류를 확인하세요."
}
"서버 패키지: $archive\WindowsServer"
"서버 내부 exe: $archive\WindowsServer\ArtisticSW2026\Binaries\Win64\ArtisticSW2026Server.exe"
```

소스 엔진의 앞서 설명한 MSVC 14.44 XML 설정이 필요하다. 소스 에디터와 서버를 빌드하고 WindowsServer Cook, Stage, Pak/IoStore, Package, Archive를 수행한다. 서버 패키징 성공과 기존 Launcher 에디터 모듈 복구 성공을 모두 검사한다.

## 방 생성 전에 서버 실행 방식 선택

Development 클라이언트 또는 에디터 -game의 콘솔에서 설정한다.

```text
sw.Room.ServerBackend 0
```

0은 기존 에디터 서버를 명시적으로 선택한다. 기존 SWLocalEditorServer 및 SWRoomEditorExe/SWRoomProject 경로를 사용한다.

```text
sw.Room.ServerExe "C:/Unreal Projects/ArtisticSW2026/Packaged/Server/WindowsServer/ArtisticSW2026/Binaries/Win64/ArtisticSW2026Server.exe"
sw.Room.ServerBackend 1
```

1은 기본값인 패키징 서버다. 위 경로도 CVar의 기본값이다. 경로는 원하는 날짜별 패키지의 실제 내부 exe로 바꿀 수 있다. 루트 WindowsServer/ArtisticSW2026Server.exe는 bootstrap이므로 지정하지 않는다. exe가 들어 있는 폴더의 상대 구조와 Content/Paks를 포함한 WindowsServer 전체 폴더를 유지한다.

서버 exe 경로 우선순위는 비어 있지 않은 sw.Room.ServerExe → 실행 인자 -SWRoomServerExe → DefaultGame.ini의 [SWLocalPackagedServer] ServerExecutable이다. 콘솔에서 `sw.Room.ServerExe ""`로 CVar 경로를 초기화하면 실행 인자/설정 경로를 사용한다. CVar 기본 경로는 프로젝트의 Packaged/Server 내부 exe이며, BuildServer.bat의 최신 날짜별 폴더로 자동 전환하지 않는다.

CVar 변경은 다음 방 생성·이어하기에 적용한다. 공인 IP 확인 중이거나 방 서버가 이미 실행 중일 때 변경해도 진행 중인 방의 서버를 바꾸지 않는다. 잘못된 backend 또는 경로는 오류/호스트 버튼 비활성 상태로 처리하며 에디터로 자동 fallback하지 않는다. 실행 로그의 Backend=Editor/Packaged와 Executable로 실제 선택을 확인한다.

## 변경된 Development 클라이언트 패키징

새 CVar를 Packaged/Windows/ArtisticSW2026.exe에서 사용하려면 해당 클라이언트도 다시 패키징한다. 아래 명령은 현재 프로젝트의 Game 타깃을 Launcher 엔진으로 패키징하며 기존 Packaged/Windows 결과를 갱신한다. 기존 결과 보존이 필요하면 archive 경로를 다른 폴더로 지정한다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat' `
  BuildCookRun `
  '-project=C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject' `
  '-unrealexe=C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  -noP4 -unattended -utf8output -platform=Win64 -clientconfig=Development `
  -build -cook -stage -pak -iostore -package -archive `
  '-archivedirectory=C:\Unreal Projects\ArtisticSW2026\Packaged'
```

이번 CVar 추가 작업에서는 위 클라이언트 Cook/Package 명령을 실행하지 않았다. 컴파일 검증과 실제 접속·저장·속도 검증은 구분한다.

재패키징한 클라이언트를 패키징 서버 모드로 시작하는 예:

```powershell
$client = 'C:\Unreal Projects\ArtisticSW2026\Packaged\Windows\ArtisticSW2026.exe'
$arguments = '-log -windowed -ResX=1000 -ResY=700 -ExecCmds="sw.Room.ServerBackend 1" -SWRoomServerExe="C:\UnrealBuilds\ArtisticSW2026Server-UE574\WindowsServer\ArtisticSW2026\Binaries\Win64\ArtisticSW2026Server.exe"'
Start-Process -FilePath $client -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $client)
```

`-ExecCmds="sw.Room.ServerBackend 0"`이면 같은 클라이언트에서 에디터 서버를 사용한다. 창 크기와 클라이언트 실행 파일 선택은 서버 backend와 독립이다.
