# 언리얼 엔진 GPU 하드웨어 효율 프로파일링 & 최적화 완벽 가이드
> **대상 프로젝트**: ArtisticSW2026 (`SWShipWake.ush` & `M_Realistic_Water`)  
> **테스트 환경**: AMD Ryzen 7 7800X3D / AMD Radeon RX 9070 XT (RDNA 4) / Unreal Engine 5.7 (DirectX 12)  
> **분석 도구**: AMD Radeon Developer Tool Suite (RDP, RGP, RGA)

---

## 📌 목차
1. [GPU 사용량(Time) vs GPU 사용 효율(Efficiency)의 차이](#1-gpu-사용량time-vs-gpu-사용-효율efficiency의-차이)
2. [RDP(Radeon Developer Panel) 설정 및 언리얼 연동](#2-rdpradeon-developer-panel-설정-및-언리얼-연동)
3. [RGP(Radeon GPU Profiler)를 통한 실전 병목 분석](#3-rgpradeon-gpu-profiler를-통한-실전-병목-분석)
4. [언리얼 머티리얼 통계와 코드 레벨의 원인 규명](#4-언리얼-머티리얼-통계와-코드-레벨의-원인-규명)
5. [RGA(Radeon GPU Analyzer) 정밀 셰이더 컴파일 분석](#5-rgaradeon-gpu-analyzer-정밀-셰이더-컴파일-분석)
6. [최종 최적화 솔루션 및 아키텍처 가이드](#6-최종-최적화-솔루션-및-아키텍처-가이드)

---

## 1. GPU 사용량(Time) vs GPU 사용 효율(Efficiency)의 차이

### 왜 `stat GPU`, `ProfileGPU`만으로는 부족한가?
* **GPU 시간(`stat GPU`, `ProfileGPU`)**: "어떤 렌더 패스가 몇 ms 걸렸는가?"만 알려주는 거시적(Macro) 지표입니다.
* **GPU 효율(Hardware Efficiency)**: "GPU 내부의 수천 개 연산 유닛(ALU), 캐시 메모리, 대역폭을 낭비 없이 100% 쥐어짜고 있는가?"를 측정하는 미시적(Micro) 지표입니다.

### 핵심 하드웨어 효율 지표
1. **Wavefront Occupancy (점유율)**: GPU의 연산 슬롯(SIMD 유닛)에 실행 가능한 스레드(Wavefront)가 얼마나 꽉 차 있는가?
2. **VGPR (Vector General Purpose Register)**: 셰이더 하나가 사용하는 레지스터 개수. 변수가 너무 많으면 점유율이 급락함.
3. **Wavefront Duration (웨이브프론트 수명)**: 스레드가 작업을 끝내는 데 걸리는 시간 (일반 셰이더: 1~5 μs).
4. **Memory Coalescing & Texture Lookups**: 픽셀당 텍스처 메모리를 얼마나 자주 읽고 대기(Stall)하는가?

---

## 2. RDP(Radeon Developer Panel) 설정 및 언리얼 연동

### 2.1. RDP 초기 설정
RDP는 실행 중인 DirectX 12 / Vulkan 프로세스를 감지하여 초정밀 하드웨어 트레이스를 캡처하는 도구입니다.

![RDP 초기 화면](images/01_rdp_initial.png)
* `CONNECTION` 탭에서 로컬 시스템(127.0.0.1)에 연결합니다.
* `Available features`에서 **`Profiling (+)`**을 클릭하여 프로파일링 기능을 활성화합니다.

![DX12 동기화 권한 팝업](images/02_rdp_sync_popup.png)
* **Sync Primitives 권한**: DirectX 12의 GPU 큐 동기화(Signal/Wait, Fence)를 기록하기 위해 `AddUserToGroup.bat` 실행 권한을 묻는 창입니다. `Yes`를 눌러 승인합니다.

![RDP 캡처 준비 완료](images/03_rdp_ready.png)
* 상태가 **`Status: Ready`**로 바뀌면 캡처 준비가 완료된 것입니다.

---

### 2.2. 언리얼 엔진 최적 프로파일링 실행법
에디터(PIE) 상태로 캡처하면 Slate UI, 기즈모, 수십 개의 에디터 전용 패스가 섞이므로 **독립 클라이언트(`-game`)**로 실행해야 합니다.

#### 📌 추천 실행 명령어 (PowerShell / CMD)
```powershell
# [서버/클라 분리 실행 - 가장 왜곡 없는 정확한 측정]
# 서버 터미널
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "c:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject" /Game/Level/Test_Level -server -log -port=7777

# 클라이언트 터미널
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "c:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject" 127.0.0.1:7777 -game -windowed -ResX=1920 -ResY=1080 -d3d12
```

---

## 3. RGP(Radeon GPU Profiler)를 통한 실전 병목 분석

게임 내에서 병목 상황으로 이동한 후 단축키 **`Ctrl + Alt + C`**를 누르면 1프레임 스냅샷(`.rgp`)이 생성됩니다.

![RGP 캡처 목록](images/06_rdp_captured_list.png)

> **💡 왜 초 단위 로깅이 아니라 1프레임 캡처인가?**  
> RGP는 클럭/스레드 단위로 나노초 레벨의 하드웨어를 추적하므로 데이터양이 방대합니다. 따라서 문제의 순간 1프레임을 현미경처럼 정밀 해부하는 스냅샷 방식을 사용합니다.

---

### 3.1. 프레임 개요 (Overview) 분석
![RGP Overview](images/07_rgp_overview_breakdown.png)

* **하드웨어 사양**: AMD Ryzen 7 7800X3D + **Radeon RX 9070 XT**
* **Frame duration**: `9.94 ms` (약 100.6 FPS)
* **GPU Idle**: `13.70%` (GPU가 다음 프레임 명령을 대기하며 유휴)
* **파이프라인 비중**: `Dispatch (Compute Shader) 453회` vs `DrawIndexedInstanced (Raster) 200회`

---

### 3.2. Most Expensive Events (병목의 주범 탐색)
![Most Expensive Events](images/08_rgp_most_expensive.png)

* **파레토 법칙 (80/20)**: 상위 5%의 이벤트가 **프레임 전체 시간의 86%를 독점**하고 있음.
* **압도적인 1위 범인 (Event 1262)**:
  * `Duration`: **`5,714 μs (5.71 ms)`** (전체 프레임 시간의 **58%**를 단 하나의 드로우 콜이 차지!)
* **2위 (Event 1261)**: `1,543 μs (1.54 ms)`
* **3위 (Event 1321)**: `420 μs (0.42 ms)` (Compute Shader Dispatch)

---

### 3.3. 하드웨어 가동률 (Wavefront Occupancy) 정밀 진단
![Wavefront Occupancy Timeline](images/09_rgp_occupancy_timeline.png)
* 타임라인 중앙의 **길고 평평한 파란색 블록(2.6ms ~ 8.2ms)**이 바로 Event 1262 실행 구간입니다.
* GPU 파이프라인을 100% 꽉 채우지 못하고 **25~35%의 낮은 점유율로 길게 늘어져서 실행**되고 있습니다.

![선택 구간 통계](images/10_rgp_selection_stats.png)
* **`Mean wavefront duration: 180.069 μs` (★극단적 수치★)**:
  * 정상적인 언리얼 픽셀 셰이더의 스레드 수명은 **1 ~ 5 μs**입니다.
  * 무려 **180 μs**가 걸린다는 것은 픽셀 셰이더 내부에 거대한 루프, 복잡한 수학 공식, 다수의 텍스처 읽기가 돌고 있다는 확실한 증거입니다.

---

### 3.4. Event 1261과 Event 1262의 세부 비교
| 항목 | Event 1261 | Event 1262 |
| :--- | :--- | :--- |
| **스크린샷** | ![Event 1261](images/11_rgp_event1261_detail.png) | ![Event 1262](images/12_rgp_event1262_detail.png) |
| **Duration** | `1.54 ms` | `5.71 ms` |
| **PS Hash** | `0x942D7FFB395F7015B4D19C8FB9C0F5D6` | `0x942D7FFB395F7015B4D19C8FB9C0F5D6` (**동일**) |
| **PSO Hash** | `0xF03FA771C2A50359` | `0xF03FA771C2A50359` (**동일**) |
| **VGPR 사용량** | **`144 (144)`** | **`144 (144)`** |
| **Occupancy** | **`5 / 16 (31.25%)`** | **`5 / 16 (31.25%)`** |
| **렌더링 픽셀 수** | 207,171 픽셀 | 724,617 픽셀 |

> **핵심 결론**: Event 1261과 1262는 **동일한 머티리얼(`M_Realistic_Water`)**을 사용하는 타일들입니다.  
> 화면의 약 93만 픽셀을 칠하면서 **합계 `7.25 ms` (프레임의 73%)**를 이 단 하나의 머티리얼이 먹고 있었습니다.

---

## 4. 언리얼 머티리얼 통계와 코드 레벨의 원인 규명

### 4.1. 머티리얼 에디터 통계 확인
![언리얼 머티리얼 통계](images/13_ue_material_stats.png)

* **`Texture Lookups (Est.): PS(59)` (★가장 치명적인 원인★)**:
  * 픽셀 하나를 그릴 때 **텍스처 메모리를 59번이나 읽고 있습니다.** (일반 머티리얼: 5~10회).
  * 59번의 텍스처 주소와 데이터를 보관하느라 **VGPR이 144개까지 폭증**한 것입니다.
* **`Base pass vertex shader: 882 instructions & VS(13)`**:
  * 파도 높이(WPO) 연산으로 인해 버텍스 셰이더도 882개의 명령어를 소모 중입니다.

---

### 4.2. `SWShipWake.ush` 코드 분석 (Smoking Gun)
`M_Realistic_Water` 내부에서 호출되는 `SWShipWake.ush`의 매크로 루프 구조:

```hlsl
// SWShipWake.ush
#define SW_M7_WAKE_CAPACITY 256

[loop]
for (int SW_Index = 0; SW_Index < SW_M7_WAKE_CAPACITY; ++SW_Index)
{
    if (SW_Index >= SW_Count) break;
    
    // 루프 매 반복마다 텍스처를 4회 샘플링!
    const float4 SW_R0 = Texture2DSampleLevel(EVENT_TEX, EVENT_SAMPLER, float2(SW_X, 0.125), 0.0);
    const float4 SW_R1 = Texture2DSampleLevel(EVENT_TEX, EVENT_SAMPLER, float2(SW_X, 0.375), 0.0);
    const float4 SW_R2 = Texture2DSampleLevel(EVENT_TEX, EVENT_SAMPLER, float2(SW_X, 0.625), 0.0);
    const float4 SW_R3 = Texture2DSampleLevel(EVENT_TEX, EVENT_SAMPLER, float2(SW_X, 0.875), 0.0);

    // 2차 방정식 풀이, sqrt, exp, cos, sin, normalize, smoothstep...
    ...
    // 골든 텍스처 1회 추가 샘플링
    SW_Golden = Texture2DSampleLevel(GOLDEN_TEX, GOLDEN_SAMPLER, SW_GoldenUV, 0.0).r * SW_StampMask;
}
```

* **문제점**: 화면의 모든 픽셀마다 최대 256번 루프를 돌며 매번 5번의 텍스처 샘플링과 무거운 삼각함수/2차 방정식 수학을 실시간으로 계산하고 있었습니다.

---

## 5. RGA(Radeon GPU Analyzer) 정밀 셰이더 컴파일 분석

### 5.1. RGA GUI와 CLI의 차이점
* **RGA GUI (Vulkan 모드)**: `glslang` 파서를 사용하여 언리얼의 HLSL 매크로(`#pragma once` 등)나 한글 경로(`OneDrive\문서`)에서 에러가 발생할 수 있습니다.
* **RGA CLI (`rga.exe -s dx12`)**: Microsoft 공식 DXC 컴파일러를 내장하여 언리얼 HLSL 코드를 순수 하드웨어 레벨(RDNA 4 ISA)로 완벽하게 컴파일 및 분석합니다.

---

### 5.2. RGA DX12 실행 명령어 및 정밀 분석 결과
[TestWaterShader.hlsl](file:///c:/Unreal%20Projects/ArtisticSW2026/Shaders/TestWaterShader.hlsl) 래퍼를 생성한 후 아래 명령어로 정밀 분석을 수행했습니다:

```powershell
& "C:\Users\vlvkr\Downloads\RadeonDeveloperToolSuite-2026-05-28-1806\RadeonDeveloperToolSuite-2026-05-28-1806\rga.exe" `
    -s dx12 `
    -c gfx1201 `
    --ps "c:\Unreal Projects\ArtisticSW2026\Shaders\TestWaterShader.hlsl" `
    --ps-entry MainPS `
    --ps-model ps_6_0 `
    -a "c:\Unreal Projects\ArtisticSW2026\Shaders\gfx1201_rga_stats_pixel.csv" `
    --isa "c:\Unreal Projects\ArtisticSW2026\Shaders\gfx1201_rga_isa_pixel.txt"
```

#### 📊 RGA 최종 하드웨어 리포트 (`gfx1201_rga_stats_pixel.csv`)
```text
Statistics:
    - resourceUsage.numUsedVgprs   = 204   <-- (★최대 한도 256개 중 204개 독식★)
    - resourceUsage.numUsedSgprs   = 96
    - numAvailableVgprs            = 256
    - numPhysicalVgprs             = 1536
    - RDNA ISA 어셈블리 파일 크기  = 910 KB
```

* **진단**: 픽셀 셰이더 하나가 **204개의 VGPR**을 사용하여 GPU 하드웨어 자원을 고갈시키고 점유율을 10~20%대로 떨어뜨리고 있음을 명확하게 검증했습니다.

---

## 6. 최종 최적화 솔루션 및 아키텍처 가이드

### 🚀 해결책: Compute Shader 오프스크린 베이킹 (업계 표준)

```text
[ 기존 방식 (심각한 병목) ]
화면의 93만 개 픽셀마다 ──> [SWShipWake.ush 256 루프 + 59회 텍스처 룩업] ──> 7.25 ms 소요 (VGPR 204)

[ 최적화 방식 (권장) ]
1프레임에 딱 1번 실행 ──> [SWShipWakeSubsystem (Compute Shader)] ──> 512x512 렌더타깃에 1회 베이킹 (0.05 ms)
화면의 93만 개 픽셀    ──> [M_Realistic_Water] ──> 구워진 텍스처 1번 샘플링 (0.05 ms)
총 소요 시간: 7.25 ms  ──>  0.1 ms 이하 (약 70배 성능 향상!)
```

### 세부 최적화 체크리스트
1. **렌더타깃 분리**: `SWShipWakeSubsystem`에서 파형/노멀 연산을 Compute Shader로 `RenderTarget2D` (R16F / RGBA16F)에 프레임당 1회 렌더링.
2. **머티리얼 간소화**: `M_Realistic_Water`의 Custom 노드/루프를 제거하고, 베이킹된 텍스처를 단 1회의 `TextureSample`로 교체.
3. **Vertex Interpolator 활용**: 버텍스 셰이더와 픽셀 셰이더 간 중복 계산되는 좌표/감쇠 연산을 보간기로 공유.
4. **루프 제한 (임시 조치 시)**: `SW_M7_WAKE_CAPACITY`를 256에서 `8 ~ 16`으로 축소 및 조기 탈출(Early Out) 추가.
