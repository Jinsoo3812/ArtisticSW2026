# Lvl_CY Nanite와 Insights 측정

UE 5.7 기준. 계측은 기존 AI·물리·Tick 동작을 바꾸지 않는다. Shipping에서는 캡처 명령과 자동 캡처를 사용하지 않는다.

## 적용한 최적화와 유지할 설정

| 대상 | 적용값 / 변경 범위 |
|---|---|
| `Lvl_CY` Landscape | Nanite 활성화 및 데이터 빌드·저장, 컴포넌트 32개 검증 |
| `M_Landscape` | Nanite 사용 플래그 활성화 |
| `LandscapeGrassType01`의 풀·꽃 품종 | Cast Dynamic Shadow 끄기, Cast Contact Shadow 켜기 |
| 위 품종의 WPO Disable Distance | 3000cm(30m). 원거리 바람 변형을 제한하며 메시를 숨기는 거리는 아님 |
| 맵 Directional Light의 Contact Shadow | 월드 공간 길이 20cm, Casting Intensity 0.75, Non-Casting Intensity 0 |
| 프로젝트 그림자 설정 | `grass.DisableDynamicShadows=0`, `r.ContactShadows=1`, `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages=0` |

전역 풀 그림자를 끄는 대신 Grass Type별로 동적 그림자 맵 생성을 줄였다. Contact Shadow는 화면 공간 기반이므로 화면 밖 물체나 긴 노을 그림자를 완전히 대신하지 못한다. 근접 풀·꽃의 접촉감과 카메라 이동 시 품질을 확인한다. 비 Nanite coarse page 제외 설정은 안개·반투명 표면의 그림자도 확인해야 한다.

풀 밀도, 기존 LOD와 컬링 거리는 유지했다. 풀의 Start/End Cull Distance는 15000/20000cm, 꽃은 10000/10000cm다. PCG 그래프와 PCG 메시·머티리얼은 수정하지 않았다. Landscape Nanite 활성화는 이 식물 메시의 Nanite 활성화와 별개다.

에디터 화면에서 Shadow Depths가 약 7ms에서 0.3~0.6ms로 줄어든 표본이 관찰됐다. 다만 카메라·시간·뷰포트 조건이 모두 통제된 비교가 아니므로 출시 성능 개선율이나 FPS 두 배 향상의 근거로 사용하지 않는다. 다음 검증은 동일 카메라·해상도·품질에서 **시간 고정 / 실제 주야간 진행**을 각각 측정하고, 최소 사양 GPU의 Development 패키지에서 반복한다. 날씨 Blueprint는 원래 자동 시간 진행 연결로 복원한 상태다.

## 에디터에서 캡처

1. `Lvl_CY`를 열고 PIE를 실행한다. 노을 시간을 고정하고, 같은 위치·카메라·해상도를 사용한다. 셰이더 컴파일과 지형/풀 스트리밍이 끝난 뒤 측정한다.
2. 바다 방향에서 콘솔에 `sw.Insights.Start Sunset_Sea 20`을 입력한다. 카메라를 유지하면 20초 뒤 자동 저장된다.
3. 산 방향으로 돌아서 `sw.Insights.Start Sunset_Mountain 20`을 입력한다.
4. `Saved/Profiling/Insights`의 `.utrace`를 Unreal Insights에서 연다. 같은 이름의 `.json`에 카메라, 해상도, Nanite, 그림자 설정과 실제 캡처 시간이 기록된다.

`sw.Insights.Mark Mountain`은 현재 카메라를 북마크로 남긴다. `sw.Insights.Stop`은 이 월드가 시작한 캡처를 일찍 종료한다. 기존 Insights 녹화가 연결돼 있으면 Start는 거부한다. 다른 녹화를 자동으로 끊지 않는다. PIE를 종료해도 캡처와 named events 설정은 정리된다. 캡처 시간은 월드 시간이 아닌 실제 시간이라 게임을 일시정지해도 종료된다.

Unreal Insights가 실행 중이면 에디터가 로컬 트레이스 서버에 자동 연결될 수 있다. `Another trace is active`가 나오면 `Trace.Status`로 확인하고, 기존 녹화를 끝내도 되는 경우 `Trace.Stop` 뒤 Start를 실행한다. 자동 실행 스크립트는 `-traceautostart=0`으로 이 자동 연결을 예방한다. `stat` 명령은 화면 통계를 표시하며 자체적으로 `.utrace`를 저장하지 않는다.

새 에디터 창 PIE로 측정할 때는 배경 레벨 뷰포트의 Realtime을 끄고, 그래프에서 프레임마다 `SceneRender`가 몇 번 실행되는지 확인한다. 에디터 뷰포트와 게임 화면이 함께 렌더링되면 두 화면의 GPU 비용이 섞인다. 캡처 중 카메라를 유지하고 JSON의 실제 해상도와 품질 배율도 동일하게 맞춘다.

## 무엇을 볼지

Timing Insights에서 `SW Capture Begin` ~ `SW Capture End` 사이를 선택한다. 첫 몇 초의 스트리밍/PSO 생성 구간이 있다면 제외하고 비교한다.

| 트랙/검색 이름 | 확인할 비용 |
|---|---|
| GameThread / `SW_EnemyShip_Tick`, `SW_NavalAI_*` | 적 배 Tick·타깃 탐색 |
| `SW_Swarm_*`, `SW_Navigation_*` | 거리 최적화·군집 회피·항해 |
| `SW_Ship_Tick`, `SW_Buoyancy_*` | 배 Tick·부력·수면 쿼리 |
| 물리 스레드 / `SW_Ship_AsyncPhysics` | Chaos 배 시뮬레이션 |
| `SW_Swimming_*` | 캐릭터 수영·수면 쿼리 |
| `SW_Wake_*`, 기존 Ripple scope | 물결 처리·텍스처 업로드·Compute 제출 |
| GameThread의 Blueprint/Tick/로그 이벤트 | 날씨 Blueprint 등 C++ 밖의 비용 |
| GPU / Shadow Depths, Basepass, Render Velocities | 그림자와 지형/풀 렌더링 비용 |
| RenderThread / RHIThread | 렌더 명령 제출과 대기 |

**Inclusive는 자식 scope를 포함한다.** EnemyShip Tick 안에 Ship Tick이 포함되므로 합산하지 않는다. GPU Compute 큐의 Wait는 셰이더 실행 비용과 구분한다. Game/Draw/GPU 시간도 서로 합산해 Frame 시간을 계산하지 않는다.

## 자동 PIE 실행과 Nanite A/B

기본 실행 경로는 별도 에디터의 PIE다. 이 프로젝트의 UE 5.7 uncooked `-game` 실행에서는 저장된 지형 Nanite가 로드 중 무효화되는 현상이 관찰됐다. `-Standalone`을 지정하면 해당 경로를 검사할 수 있지만, Nanite 컴포넌트가 없는 On 캡처는 스크립트가 실패로 보고한다. Editor 검증과 실제 런타임 상태는 JSON의 `nanite_components` 및 `nanite_up_to_date`로 구분한다.

에디터와 빌드가 끝난 상태에서 프로젝트 루트의 PowerShell에서 실행한다.

```powershell
./Scripts/Run-LevelInsights.ps1 -Label Sunset_NaniteOn -LandscapeNanite On
./Scripts/Run-LevelInsights.ps1 -Label Sunset_NaniteOff -LandscapeNanite Off
```

이 스크립트는 맵에 저장된 에디터 Perspective 카메라를 사용한다. 원하는 장소를 에디터에서 보고 맵을 저장한 뒤 실행한다. `-YawOffset 180`으로 반대 방향을 측정할 수 있다. `-Pitch`, `-CameraZOffset`, `-Width`, `-Height`, `-Warmup`, `-Seconds`도 지정 가능하다. `-Offscreen`은 화면에 게임 창을 띄우지 않고 실제 DX12 GPU로 렌더링한다. 저장된 PNG로 실제 시점을 확인한다. 패키지에는 에디터 카메라가 없으므로 PlayerStart를 비교 위치에 배치한다. 고정 카메라는 플레이어의 위치와 AI 인지/스트리밍 소스를 이동시키지 않으므로, 플레이어 주변 AI 비용 비교에는 실제 플레이어를 동일 위치에 두고 PIE에서 측정한다.

두 번 모두 저장된 날씨 설정을 사용한다. 시간 진행을 고정해야 같은 조건이 된다. 프로파일링 때문에 적·그림자·풀·디버그 표시를 임의로 끄지는 않는다. Script의 Off는 `Landscape.RenderNanite`만 바꾼다. Nanite 데이터는 여전히 로드될 수 있으므로 **메모리/패키지 용량 비교에는 이 A/B를 사용하지 않는다.** Nanite 지형이 풀이나 PCG 메시까지 Nanite로 바꾸지는 않는다.

## RT 지오메트리 메모리 예산

팀 공통 초기 예산은 `Config/DefaultEngine.ini`의 `[SystemSettings]`에서 `r.RayTracing.ResidentGeometryMemoryPoolSizeInMB=512`로 지정한다. 관측된 상주 지오메트리 약 432MiB보다 큰 512MiB(0.5GiB)를 초기 기준으로 사용하며, 출시 최종 예산은 아직 확정하지 않는다. 에디터를 재시작한 뒤 콘솔에서 값 없이 같은 변수 이름을 입력해 적용값을 확인한다. 이 설정은 패키징되는 프로젝트 기본 설정에도 포함되며, 콘솔·Device Profile 등의 상위 우선순위 설정이 있으면 덮어쓸 수 있다.

이는 전체 VRAM 제한이나 시작 시 512MiB를 고정 예약하는 설정이 아니다. 엔진은 이 예산으로 RT 지오메트리의 상주·퇴거·스트리밍을 관리한다. 항상 상주하는 지오메트리 외에 장면에서 요청하는 지오메트리도 있으므로, 현재 빨간 경고가 없어지는 것만으로 충분한 예산이라고 판단하지 않는다.

출시 검증은 실제 최소 사양 GPU에서 Development 패키지로 수행한다. `stat RayTracingGeometry`의 Always Resident / Requested / Resident Memory, 전체 VRAM 사용량, 이동·카메라 회전 중 프레임 급증을 함께 확인한다. 가장 무거운 레벨·전투·주야간 전환에서 예산 부족이나 반복 빌드가 확인되면 768 또는 1024MiB와 비교한다. 고사양 개발 PC의 VRAM 용량만으로 모든 사용자에게 1~2GiB를 할당하지 않는다. Shipping에서 경고가 표시되지 않더라도 예산 부족이 해결된 것으로 해석하지 않는다.

## Nanite 데이터 빌드·저장

Enable Nanite 체크만으로는 저장된 Nanite 메시가 완성되지 않을 수 있다. 에디터에서는 Landscape의 **Build Data**를 실행하고 맵을 저장한다. 다음 도구는 지정한 비 World Partition 맵만 백업한 뒤, 엔진의 동기 빌드 경로로 완료를 기다리고 맵을 저장한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' ./ArtisticSW2026.uproject -run=SWBuildLandscapeNanite -Map=/Game/Level/Lvl_CY -EnableNanite -unattended -nullrhi -nosound -nop4
# 새로운 프로세스에서 저장 상태만 검증 (변경하지 않음)
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' ./ArtisticSW2026.uproject -run=SWBuildLandscapeNanite -Map=/Game/Level/Lvl_CY -CheckOnly -unattended -nullrhi -nosound -nop4
```

백업: `Saved/Optimization/NaniteBackup/<GUID>/Lvl_CY.umap`. 빌드 중 새 빌드/에디터를 실행하거나 프로세스를 강제 종료하지 않는다. 지형 변경 뒤에는 다시 Build Data → 저장 → 검증한다.

## 2026-10-04 검증 기록

- 직접 UBT로 `ArtisticSW2026Editor Win64 Development` 빌드 성공. 최종 빌드에 C++ 경고/오류 없음.
- 맵 백업 후 Nanite 데이터 저장, 새 에디터 프로세스에서 `Enabled=1 UpToDate=1` 검증.
- 실제 RX 9070 XT / DX12 PIE 15초 캡처: Nanite 컴포넌트 32개, 최신 데이터 확인, 자동 종료 성공. 트레이스와 설정 JSON 저장 및 Unreal Insights의 CSV 통계 추출 확인.
- 첫 PIE 표본: `SW_Ship_Tick` 평균 약 0.035ms/호출, `SW_Wake_Tick` 약 0.038ms/호출. 해당 시점의 적 배 수는 0이므로 AI 부하를 판단하는 표본은 아니다. viewport는 1061×550이며 에디터 비용이 섞여 있다. On/Off 성능 향상을 입증하는 A/B 결과로 해석하지 않는다.
- PIE 해상도는 에디터 플레이 설정/뷰포트 크기를 따른다. 스크립트의 Width/Height는 Standalone 실행용이며, 비교 전 JSON의 실제 viewport 크기를 확인한다.
- 프로젝트의 기존 아이템 태그·텍스처·충돌 에셋 경고/오류는 런타임 로그에 남아 있다. 이번 C++ 계측 변경으로 해결됐다고 간주하지 않는다.
- 커밋 전 날씨 Blueprint의 재생 속도를 0배로 만드는 테스트 연결을 발견해 원래 연결로 복원했다. 기존 `Sunset_Mountain_TimeRunning` 파일은 시간 진행이 실제로 활성화됐다는 검증 자료로 사용하지 않는다. 카메라 이동·해상도 변경·두 화면 렌더링도 포함되어 있으므로 동일 조건 개선율을 계산하지 않는다.
- 커밋 직전 직접 UBT 재빌드 성공(C++ 경고·오류 없음). 보완한 자동 실행 도구로 약 5초 PIE 캡처가 정상 종료됐고, 고정 카메라·RT 예산 512·Nanite 컴포넌트 32개/최신 상태와 추가 품질 배율 메타데이터 저장을 확인했다. 현재 로컬 품질 배율은 View Distance / Grass Density / Foliage Density 모두 0.8이다.

엔진 참고: [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5), [Landscape Nanite](https://dev.epicgames.com/documentation/unreal-engine/using-nanite-with-landscapes-in-unreal-engine?lang=en-US).
