# Bombardment(포탄 세례) 스킬 구현 및 테스트 기록

## 1. 확정 요구사항

- 영문 이름: `Bombardment`
- 한국어 표시명: `포탄 세례`
- 배를 직접 조종하는 상태에서만 테스트 키 `5`로 조준 모드 진입
- 조준 중 WASD 선박 운항은 유지하고 마우스 카메라 회전만 중단
- 마우스 커서를 따라 원형 프리뷰 표시
- 적 배와 일반 적 캐릭터는 프리뷰 범위 안에서 로컬 사용자에게 하이라이트
- 실제 포탄 피해는 기존 대포알 규칙을 유지하므로 현재는 적 배만 피해
- 일반 적 범위 피해와 폭발은 후속 대포 폭발 작업에서 추가
- 바다는 안정적인 Water Body 기준면을 사용하고 파도 높이는 계산하지 않음
- Landscape 섬을 직접 가리키면 Landscape 충돌 지점을 유효 표면으로 사용
- 적, 적 배 및 기타 동적 오브젝트는 조준 표면 계산에서 무시
- 좌클릭 확정, `5` 재입력·우클릭·Esc로 취소
- 포탄 클래스/메시는 Player 배의 일반 대포에서 재사용
- 데미지와 포탄 비행 속도는 발사 시점의 Player 배 ASC 속성을 사용
- 짧은 묶음 발사 시간, 묶음 간격, 묶음 수, 묶음당 포탄 수는 Bombardment 자체 설정

## 2. 구현 구조

### 2.1 Gameplay Ability

`UGA_Bombardment`는 Player ASC에 기본 부여되는 ServerOnly GA다.

- Asset Tag: `GameplayAbility.Skill.Bombardment`
- 배의 숫자 `5` 입력이 RidingPlayer ASC에서 태그 기반으로 활성화를 요청한다.
- GA는 Avatar인 Player가 현재 붙어 있는 상위 Actor에서 `AShip`을 찾는다.
- Player를 실제 RidingPlayer로 보유하고 PlayerController가 조종 중인 배만 허용한다.
- 활성화 중에는 Loose Gameplay Tag를 유지한다.
- 취소 또는 좌클릭 확정 후 GA가 종료되면 배의 조준 상태도 정리한다.

### 2.2 AShip 입력·조준·서버 검증

`AShip`이 실제 조종 Pawn이므로 테스트 입력과 커서 처리는 배에 둔다.

- `5`: Bombardment GA 토글
- 좌클릭: 현재 유효 위치 확정
- 우클릭/Esc: 취소
- `ShipLook()`은 Bombardment 조준 중 즉시 반환한다.
- `ShipMove()`와 `ShipTurn()`은 변경하지 않아 WASD 운항이 유지된다.
- 조준 시작 시 GameAndUI 입력 모드와 커서를 활성화한다.
- 조준 종료 시 기존 커서 표시 상태와 GameOnly 입력 모드를 복원한다.
- 하선, UnPossess, EndPlay에서는 강제 취소 및 프리뷰 정리를 수행한다.

클라이언트가 보낸 Z는 신뢰하지 않는다. 서버는 받은 XY에서 Water Body/Landscape 표면을 다시 구하고 `MaxTargetRange`를 재검증한다.

### 2.3 공용 해수면 조회

`FWaterSurfaceQueryLibrary`를 WaterAndShip 모듈의 공용 유틸리티로 추가했다.

- 모든 Water Body 중 해당 XY에서 조회 가능한 가장 높은 수면을 반환한다.
- River는 가장 가까운 Spline Input Key를 사용한다.
- `bIncludeWaveHeight` 옵션을 제공한다.
- Bombardment는 항상 `false`로 호출한다.
- 기존 Gravity Vortex 투사체는 기존 설정값을 전달하여 동작을 보존한다.

성능 특성:

- 단순 Collision Ray 한 번과 완전히 동일한 비용은 아니며 Water Plugin 위치 조회가 들어간다.
- 파도 Gerstner 계산을 생략하므로 위치 조회 자체는 가볍다.
- 현재처럼 Water Body가 한두 개인 맵에서 로컬 조준 프레임마다 몇 번 호출하는 것은 실질적인 병목 가능성이 낮다.
- Water Body가 매우 많아진다면 AShip이 현재 Ocean을 캐시하도록 최적화할 수 있다.
- 현재 프리뷰 비용 중에서는 0.1초마다 수행하는 적 하이라이트 탐색이 수면 조회보다 먼저 프로파일링할 대상이다.

### 2.4 바다와 Landscape 표면 선택

바다 조준:

1. `DeprojectMousePositionToWorld`로 커서 Ray 생성
2. 배 위치에서 구한 안정적인 Water Z와 Ray/Plane 교차
3. 교차 XY에서 Water Body를 다시 조회해 정확한 기준면 Z 결정

Landscape 조준:

1. Water Body Actor와 현재 배/탑승자를 무시하고 WorldStatic만 조회하는 Object Multi Trace 실행
2. `ALandscapeProxy` Hit만 선택
3. Landscape Hit이 바다 평면 교차보다 카메라에 가깝고 수면보다 위라면 Landscape Hit 사용
4. 서버는 확정 XY에서 위아래 수직 Trace를 다시 수행해 Landscape Z를 재결정

이 때문에 커서 아래 적 배가 먼저 보이더라도 프리뷰가 적 배 갑판 위로 올라가지 않는다. 섬은 Landscape 표면 자체가 유효 조준점이 된다.

### 2.5 로컬 프리뷰와 하이라이트

`ABombardmentPreview`는 복제하지 않는 로컬 Actor다.

- Blueprint 자식의 `PreviewMesh`에 원형 메시와 반투명 머티리얼을 지정한다.
- Static Mesh Bounds의 XY 반경을 읽어 `SkillRadius`에 자동 스케일한다.
- `PreviewHeightOffset`만큼 표면 위에 띄운다.
- Valid/Invalid Material은 선택 사항이다. 비어 있으면 Preview Mesh의 기존 Material 0을 보존한다.
- 하이라이트는 `TargetHighlightOverlayMaterial`과 선택적인 CustomDepth/Stencil을 지원한다.
- `AShip::IsEnemyShipForEffects()`와 `ABaseCharacter::IsEnemyCharacterForEffects()`를 이용한다.
- 범위 판정은 XY 거리이며 0.1초 기본 간격으로 갱신한다.
- 범위 이탈/취소 시 기존 Overlay, CustomDepth 여부, Stencil 값을 복원한다.

### 2.6 포탄 실행 Actor

`ABombardment`는 서버에서만 존재하는 스케줄러다. 생성한 기존 `ACannonball`들이 이동 복제를 담당하므로 스케줄러 자체는 복제하지 않는다.

주요 에디터 설정:

- `SkillRadius`
- `MaxTargetRange`
- `PreviewHeightOffset`
- `PreviewClass`
- `LaunchHeightZ`
- `LaunchXYOffset`
- `ProjectileClassOverride`
- `ProjectilesPerVolley`
- `BurstSpreadDuration`
- `VolleyInterval`
- `VolleyCount`
- `DistributionMode`
- `DistributionJitter`

`ProjectileClassOverride`가 비어 있으면 현재 Player 배에 붙은 `ACannon` 중 일반 `CannonballClass`를 찾는다. 여러 대포가 서로 다른 클래스를 사용한다면 결정성을 위해 Override를 지정한다.

발사 시 데미지와 속도는 다음 Ship Attribute를 읽는다.

- `UShipAttributeSet::CannonDamage`
- `UShipAttributeSet::CannonballSpeed`

`CannonFireCooldown`은 사용하지 않는다. Bombardment 포격 주기는 전적으로 스킬 설정이 결정한다.

## 3. 포탄 위치·분포·탄도

각 포탄의 동일한 원판 상대좌표를 시작/도착에 함께 사용한다.

```text
Impact = TargetCenter + DiskOffset
Spawn  = Impact + (LaunchXYOffset.X, LaunchXYOffset.Y, LaunchHeightZ)
```

따라서 모든 포탄의 시작→도착 변위가 같고 평행한 사선 포격 모양이 만들어진다.

기존 대포알은 중력을 사용하므로 단순 LookAt 방향 대신 고정 속도 탄도해를 계산한다. 이로써 Player Ship의 실제 CannonballSpeed를 유지하면서 목표점에 도착한다. 설정된 높이·XY 오프셋·속도로 해가 없으면 해당 포탄을 생략하고 로그를 남긴다.

Landscape에서도 포탄이 지형 아래로 계속 내려가지 않도록 `ACannonball::SetDesignatedImpactLocation`을 추가했다. 포탄 이동 Segment가 지정 도착점 허용 반경에 들어오면 그 위치에서 이동·충돌·Mesh를 정지하고 제거한다. 기존 적 배 직격 피해는 그보다 먼저 그대로 처리된다.

기본 분포 `StratifiedDisk`는 같은 면적의 방사형 구간과 Golden Angle을 결합한다.

```text
r = SkillRadius * sqrt((Index + Random) / Count)
theta = RandomRotation + Index * GoldenAngle + AngleJitter
```

완전 랜덤보다 한쪽 몰림이 적지만, 매 묶음마다 Seed와 각도 Jitter가 바뀌어 규칙적인 격자로 보이지 않는다.

## 4. 묶음 발사 시간 정의

- 첫 포탄: 묶음 시작과 동시에 발사
- 마지막 포탄: 묶음 시작 + `BurstSpreadDuration`
- N이 2 이상이면 각 포탄 간격은 `BurstSpreadDuration / (N - 1)`
- `VolleyInterval`은 묶음 시작 시각끼리의 간격
- 총 예약 포탄 수: `ProjectilesPerVolley * VolleyCount`

예: N=4, Burst=0.6, VolleyInterval=2.0, VolleyCount=3

```text
0.0, 0.2, 0.4, 0.6
2.0, 2.2, 2.4, 2.6
4.0, 4.2, 4.4, 4.6
```

## 5. 자동화 테스트 결과

실행일: 2026-07-22

빌드:

```text
ArtisticSW2026Editor Win64 Development
Result: Succeeded
```

실행 명령:

```text
UnrealEditor-Cmd.exe ArtisticSW2026.uproject
  -ExecCmds="Automation RunTests ArtisticSW.Bombardment;Quit"
  -unattended -nop4 -nosplash -NullRHI
  -TestExit="Automation Test Queue Empty" -log
```

결과: 6/6 Success

| 테스트 | 검증 내용 | 결과 |
|---|---|---|
| `ArtisticSW.Bombardment.Configuration` | 기본 반경, 사거리, 높이, 포탄/묶음 수, Preview Class | Success |
| `ArtisticSW.Bombardment.ShotSchedule` | N개 분할, Burst 종료 시각, 묶음 start-to-start 간격 | Success |
| `ArtisticSW.Bombardment.StratifiedDiskDistribution` | Seed 결정성, 반경 내부, equal-area 방사 구간 | Success |
| `ArtisticSW.Bombardment.BallisticSolution` | Ship 속도 보존 및 중력 적용 후 목표 도착 | Success |
| `ArtisticSW.Bombardment.GameplayAbilityConfiguration` | GA Tag/Class와 Player 기본 Grant | Success |
| `ArtisticSW.Bombardment.GameplayAbilityShipIntegration` | 배 탑승 후 GA 활성화/취소에 따른 Targeting 상태 | Success |

자동화로 검증하지 못한 항목은 실제 Water Body, Landscape, 화면 커서, 반투명 렌더링 및 Blueprint 대포 에셋이 필요한 PIE 시각 검증이다. 해당 절차는 별도 에디터 가이드에 기록한다.

## 6. 현재 의도된 제한

- 파도 높이는 Bombardment 프리뷰/서버 목표에 포함하지 않는다.
- Landscape는 지원하지만 Static Mesh로 만든 섬은 현재 별도 지형 표면으로 선택하지 않는다.
- 일반 적 캐릭터는 하이라이트되지만 기존 대포알이 무시하므로 피해를 받지 않는다.
- 폭발, 착탄 범위 피해, 착탄 VFX/SFX는 후속 작업이다.
- GA Cost/Cooldown 에셋은 아직 지정하지 않았다.
- 테스트 숫자 5/좌클릭/우클릭/Esc는 C++ 직접 바인딩이며 최종 Enhanced Input 에셋 교체 전 임시 경로다.
