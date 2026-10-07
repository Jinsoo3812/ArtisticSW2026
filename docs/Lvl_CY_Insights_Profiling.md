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

## 2026-10-06 리터치 이후 재적용

- 현재 리터치 맵과 설정 파일을 `Saved/Optimization/RetouchRestore/<GUID>`에 백업했다. 이전 최적화 맵으로 교체하지 않고 현재 날씨 액터의 Directional Light에 Contact Shadow 월드 공간 20cm, Casting Intensity 0.75, Non-Casting Intensity 0을 재적용했다.
- 빠진 `grass.DisableDynamicShadows=0`, `r.ContactShadows=1`, `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages=0`을 복원했다. RT 예산 512MiB는 유지했다.
- 새 에디터 프로세스에서 맵을 재로드하여 조명 설정과 실제 콘솔 변수 값을 확인했다. 풀·꽃 에셋의 동적 그림자 끄기, Contact Shadow 켜기, WPO 30m 제한 및 기존 컬링 거리는 유지돼 있어 다시 수정하지 않았다.
- 그림자 재적용 직후 Landscape Nanite는 비활성 상태이며 컴포넌트는 0개였다. 이후 사용자가 Enable Nanite → Build Data → 맵 저장을 진행했고, 아래 23:29 캡처에서 최신 컴포넌트 32개를 확인했다. 위 2026-10-04의 32개 기록은 리터치 이전 검증 결과다.
- 이번 검증은 저장된 설정의 확인이며, 리터치 이후 성능 개선율을 측정한 결과는 아니다. Nanite 재적용 뒤 같은 조건으로 캡처한다.

## 2026-10-06 Retouch_Mountain 캡처 분석

- 23:29 캡처의 Nanite 컴포넌트 32개가 활성화·최신 상태이며, 복원한 공통 그림자 설정과 RT 예산 512MiB도 적용됐다. PIE Client, viewport 1836×1152, Screen Percentage 67, View Distance / Grass Density / Foliage Density는 모두 1이다.
- Unreal Insights CSV에서 앞뒤 약 2초를 제외한 중앙 1279개 graphics 프레임을 분석했다. 평균 BasePass 3.54ms, Velocity 2.28ms, ShadowDepths 2.03ms, SingleLayerWater 0.075ms다. GPU 타임라인 구간을 계산한 값이며 Compute 큐의 Wait와 합산하지 않는다.
- BasePass 내부 ParallelDraw는 3.32ms, Velocity 내부 ParallelDraw는 2.25ms다. ShadowDepths 내부 비 Nanite Batched 구간은 1.25ms, Nanite DrawGeometry는 0.59ms다. Nanite::BasePass 머티리얼 구간은 0.15ms이며 BasePass에 포함되므로 별도로 더하지 않는다.
- 중앙 구간 대부분(1277/1279 프레임)에서 SceneRender는 한 번이며, 지속적인 두 화면 렌더링이 주요 원인이라는 증거는 없다. 남은 두 프레임은 GPU 이벤트의 프레임 경계 배정 및 일시 추가 렌더링을 구분하지 않았다.
- 캡처 중 카메라가 약 2m 이동하고 yaw가 약 29도 바뀌었다. 이전 산 캡처보다 viewport 픽셀 수도 약 32% 많고, 최근 자동 검증의 품질 배율 0.8과도 다르다. 따라서 이전 결과와 개선율을 직접 계산하지 않는다.
- 현재 큰 비용은 일반 메시 드로우 경로에 있으나, 이 트레이스의 ParallelDraw에는 개별 메시·머티리얼 이름이 없어 특정 식물 에셋을 원인으로 확정하지 않는다. 다음에는 같은 카메라·시간에서 정상 표시와 `ShowFlag.InstancedGrass 0`의 임시 비교를 통해 Landscape 자동 풀 비용을 분리한다. 측정 뒤 `ShowFlag.InstancedGrass 1`로 복원하며 PCG/머티리얼 에셋은 수정하지 않는다.

## 2026-10-06 Landscape 자동 풀 표시 A/B

23:42 GrassOn / 23:43 GrassOff 캡처는 카메라 시작·종료 위치와 회전이 모두 같고, PIE Client / 1836×1152 / Screen Percentage 67 / 품질 배율 1 / Nanite 최신 컴포넌트 32개도 일치한다. 앞뒤 약 2초를 제외한 중앙 구간은 각각 473/479 graphics 프레임이며, 두 캡처 모두 프레임마다 SceneRender가 한 번이다.

| 중앙 구간 평균 | GrassOn | GrassOff |
|---|---:|---:|
| BasePass | 3.48ms | 1.03ms |
| Velocity | 2.04ms | 0.19ms |
| ShadowDepths | 2.01ms | 2.61ms |
| SceneRender | 11.50ms | 8.59ms |
| GPU 프레임 시작 간격 | 12.74ms | 12.56ms |

사용자가 시행한 `ShowFlag.InstancedGrass` 비교에서 BasePass는 약 70%, Velocity는 약 91% 감소했다. Landscape 자동 풀 표시가 이 두 패스 비용의 주요 원인이라는 근거다. 이는 메시를 숨긴 진단 결과이며, 풀을 유지하는 최적화가 같은 개선폭을 달성한다는 뜻은 아니다. PCG를 원인으로 확정하는 결과도 아니다.

그림자는 반대로 늘었으므로 이 A/B를 그림자 비용 개선의 근거로 사용하지 않는다. 날씨·태양 각도와 ShowFlag 적용값은 현재 JSON에 기록되지 않으며, 풀 숨김으로 드러난 표면과 VSM 페이지 요청 변화도 함께 고려해야 한다.

SceneRender 구간은 약 2.91ms 줄었지만 프레임 간격은 거의 같다. GameThread의 World Tick Time은 앱 프레임당 약 8.19/8.48ms, Slate Tick은 약 2.12/2.03ms이며, World Tick 호출은 앱 프레임당 약 세 번이다. PIE의 여러 월드와 CPU·에디터 비용이 전체 FPS를 제한하는 후보이므로 GPU 패스 개선을 그대로 FPS 개선으로 환산하지 않는다. 다음 풀 최적화 후보는 밀도 변경 대신 WPO 적용 거리와 머티리얼/마스크 렌더링 경로의 별도 A/B이며, 실제 FPS 검증은 동일 조건의 독립 Development 클라이언트에서 수행한다.

엔진 참고: [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5), [Landscape Nanite](https://dev.epicgames.com/documentation/unreal-engine/using-nanite-with-landscapes-in-unreal-engine?lang=en-US).

## 풀 렌더링 경로의 통제 비교

`Run-LevelInsights.ps1 -ControlledComparison`은 별도 에디터의 Standalone PIE 월드 하나를 사용한다. 백그라운드 에디터 뷰포트의 Realtime을 끄고, 날씨 액터의 `1 Hour Seconds=0` 및 `Init Hour=18`, `Init Minute=0`을 그 프로세스에서만 설정한다. 새 네트워크 날씨 코드의 Frozen 재생 경로를 사용하므로 Blueprint 타임라인 속도만 0으로 만드는 방법과 구분한다. `-ComparisonHour 12`로 낮을 비교할 수 있다. 에셋/맵/사용자의 에디터 설정을 저장하지 않는다.

`-CameraMetadata <이전 캡처 JSON>`은 이전 시점을 정확히 재사용한다. `-YawOffset 180`은 해당 시점에서 반대 방향을 본다. 플레이어·AI·스트리밍 위치는 이동시키지 않으므로 렌더링 경로 비교용이며, 전체 게임 성능 비교에는 실제 플레이어 위치도 맞춘다. 임베디드 PIE의 실제 콘텐츠 크기는 Width/Height와 다를 수 있다. **JSON의 실제 viewport 크기가 같은 캡처끼리만 비교한다.**

| RenderProfile | EarlyZPass | OnlyMaterialMasking | VelocityOutputPass |
|---|---:|---:|---:|
| Current | 프로젝트 현재값 | 프로젝트 현재값 | 프로젝트 현재값 |
| MaskedDepth | 2 | 1 | 0 (깊이 패스) |
| MaskedBaseVelocity | 2 | 1 | 1 (BasePass) |

후보는 시작 시 `-ini:Engine`으로만 적용하며 프로젝트 파일을 덮어쓰지 않는다. 이 설정들은 셰이더 순열/출력 구조에 영향을 주므로 PIE 콘솔에서 런타임 변경으로 비교하지 않는다. 최초 실행은 엔진·프로젝트 셰이더 재컴파일 때문에 수 분 이상 걸릴 수 있다. 컴파일을 중단하거나 겹쳐 실행하지 않는다. 통제 캡처는 셰이더 컴파일이 끝난 뒤 지정한 Warmup 시간 동안 준비하고, JSON에 시작/종료 `shader_jobs`를 남긴다.

```powershell
$camera = 'Saved/Profiling/Insights/<재사용할 캡처>.json'
./Scripts/Run-LevelInsights.ps1 -Label DepthBaseline -RenderProfile Current -ControlledComparison -CameraMetadata $camera -Warmup 60 -Seconds 10 -Offscreen
./Scripts/Run-LevelInsights.ps1 -Label DepthCandidate -RenderProfile MaskedDepth -ControlledComparison -CameraMetadata $camera -Warmup 60 -Seconds 10 -Offscreen
# 각 완료 JSON을 지정해 CSV로 내보낸다.
./Scripts/Export-LevelInsights.ps1 -Metadata 'Saved/Profiling/Insights/<기준 완료 캡처>.json' -OutputDirectory Saved/Profiling/Insights/RenderingAB/Current
./Scripts/Export-LevelInsights.ps1 -Metadata 'Saved/Profiling/Insights/<후보 완료 캡처>.json' -OutputDirectory Saved/Profiling/Insights/RenderingAB/Candidate
python Scripts/Analyze-LevelRendering.py Saved/Profiling/Insights/RenderingAB/Current
python Scripts/Analyze-LevelRendering.py Saved/Profiling/Insights/RenderingAB/Candidate
python Scripts/Compare-LevelRendering.py Saved/Profiling/Insights/RenderingAB/Current Saved/Profiling/Insights/RenderingAB/Candidate
```

분석은 앞뒤 2초를 제외하고 GPU graphics 타임라인을 계산한다. BasePass/Velocity뿐 아니라 PrePass와 SceneRender 평균·p95를 확인해 비용 이동을 개선으로 오해하지 않는다. 중첩 scope와 Compute 큐의 Wait를 합산하지 않는다. 비교 도구는 카메라, 태양 회전/강도, 실제 해상도, Nanite 상태, 후보 이외 CVar가 다르면 실패한다. 지속적인 복수 SceneRender도 거부하며, 프레임 경계 귀속으로 생기는 1% 이하 일시 표본은 허용한다. 기록된 태양 상태가 같더라도 구름·바람·AI의 모든 상태를 고정한 것은 아니므로, 작은 차이는 동일 Warmup으로 재측정한다.

이 경로는 풀 밀도·컬링·LOD·WPO 속도 정보 및 PCG/공유 메시·머티리얼 에셋을 유지한다. 후보 채택 전 저장된 PNG에서 잎 경계·지형·그림자를 확인하고, 플레이 중 카메라 이동과 바람에서 TSR 잔상/깜빡임을 확인한다. 프로젝트 공통 렌더러 변경은 다른 맵 및 최소 사양 Development 패키지에서도 검증한다.

## 2026-10-07 적용 결과

프로젝트 RendererSettings에 `r.EarlyZPass=2`, `r.EarlyZPassOnlyMaterialMasking=True`, `r.VelocityOutputPass=0`을 적용했다. 마스크의 투명도 판정을 깊이 단계에서 처리해 BasePass의 중복 작업을 줄이는 엔진 경로다. 속도 출력은 기존 깊이 패스를 유지한다. 풀을 숨기거나 속도 출력을 끄지 않으며, PCG/풀/공유 메시·머티리얼 에셋과 밀도·컬링·LOD·WPO 거리는 이번 변경에서 수정하지 않았다. 에디터 재시작 및 셰이더 컴파일 완료가 필요하다.

검증 환경: UE 5.7.4, RX 9070 XT, DX12, 별도 에디터 Standalone PIE, 실제 viewport 1527×982, Screen Percentage 67, 거리·밀도 배율 1, Landscape Nanite 최신 컴포넌트 32개. 각 10초 캡처의 중앙 약 6초를 사용했다. 산 비교는 동일 카메라와 18:00 고정 태양, 60초 Warmup이며 바다 비교는 반대 방향 카메라와 12:00 고정 태양, 20초 Warmup이다. 산과 바다 행끼리 개선율을 계산하지 않는다.

| 중앙 평균 (ms) | 산 기준 | 산 MaskedDepth | 바다 기준 | 바다 MaskedDepth |
|---|---:|---:|---:|---:|
| PrePass | 0.304 | 0.305 | 0.017 | 0.017 |
| BasePass | 3.569 | 2.933 | 0.461 | 0.448 |
| Velocity | 2.001 | 2.013 | 0.070 | 0.069 |
| ShadowDepths | 0.301 | 0.317 | 0.334 | 0.331 |
| SceneRender | 9.431 | 9.303 | 5.353 | 5.345 |
| SceneRender p95 | 12.979 | 12.947 | 7.210 | 7.237 |

산 BasePass는 약 18% 감소했지만 SceneRender는 약 1.4% 감소했다. 초기 후보에서 SceneRender 약 6% 감소도 관측됐으나 기준/후보 Warmup이 20/60초로 달랐고 구름·바람·AI까지 동일하지 않으므로 6%를 확정 개선율로 사용하지 않는다. 바다 방향의 전체 구간 차이는 사실상 없었다. GPU 패스 변화만으로 전체 FPS 상승을 주장하지 않는다.

`MaskedBaseVelocity` 후보는 별도 Velocity 구간을 없앴으나 PrePass 2.326ms, BasePass 3.398ms, SceneRender 9.852ms였다. 동일 60초 Warmup 기준보다 SceneRender 약 4.5% 높아 채택하지 않았다. 특정 패스가 0ms가 되는 것만으로 최적화를 판단하지 않는다.

원본 완료 캡처는 `Saved/Profiling/Insights/`의 `RenderBaselineStable_20261007_002337`, `RenderMaskedDepthStable_20261007_002550`, `RenderMaskedBaseVelocity_20261007_002106`, `RenderSeaBaseline_20261007_002753`, `RenderSeaMaskedDepth_20261007_003001` 접두사 JSON/utrace다. CSV와 분석 JSON은 `Saved/Profiling/Insights/RenderComparison/`에 있다. 반복 측정은 시작/종료 shader_jobs=0 및 카메라·태양 고정을 확인했다. 저장된 산/바다 PNG에서 잎 형태·지형·물에 뚜렷한 품질 저하는 관찰되지 않았다. 정지 이미지 비교가 이동 중 TSR 잔상 검증을 대체하지는 않는다.

직접 UBT Editor Development 빌드는 성공했다. 기존 `ChestLaunchPhysicsDiagnostic.h`의 Chaos GetRead 호출 경고 C4686은 남아 있으며, 이번 렌더링 계측 코드에서 새로운 컴파일 오류는 없었다. 기존 런타임 에셋/태그/DeckWalk 오류도 이번 변경의 해결 범위가 아니다.

프로젝트 기본값 반영 뒤 후보 INI override 없이 `RenderAppliedDay_20261007_003246`을 새 에디터에서 캡처했다. 실제 CVar 2/1/0, 시작/종료 shader_jobs=0, Nanite 최신 상태 및 정상 종료를 확인했다. 낮 시간 산 방향 PNG에서도 풀의 잎 경계와 지형을 확인했다. 측정용 날씨 변경은 맵에 저장되지 않았으며, 실행 전후 `Lvl_CY.umap` SHA-256은 동일했다.

커밋 전 원격을 갱신해 최신 디자이너 리터치가 PR #110의 `7b4d63e8` 맵임을 확인했다. 해당 LFS 원본과 현재 맵을 별도로 로드해 액터 765개의 이름·클래스·배치·스케일·bounds·Static Mesh 참조, Landscape 머티리얼·컴포넌트 구성을 비교했다. 감사용 복사 경로 때문에 달라진 WaterInfoMesh의 맵 내부 참조 접두사를 제외하면 비교 항목이 동일했다. 현재 맵은 이 리터치 배치를 유지하며 그림자 설정과 Nanite 데이터가 반영된 파생 파일이다. 원본과 바이너리가 동일하다는 의미나 지형의 모든 높이 샘플을 직접 비교했다는 의미는 아니다. 보고서는 `Saved/Optimization/DesignerMapComparison_20261007.json`에 있다.
