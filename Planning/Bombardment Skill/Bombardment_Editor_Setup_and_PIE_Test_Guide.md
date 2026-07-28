# Bombardment(포탄 세례) 에디터 설정 및 PIE 테스트 가이드

## 1. 권장 콘텐츠 폴더

에디터에서 다음 폴더를 만든다.

```text
/Game/New/Skill/Bombardment/
```

권장 에셋 이름:

```text
BP_BombardmentPreview
BP_Bombardment
GA_Bombardment
M_BombardmentPreview
M_BombardmentTargetHighlight
BP_BombardmentCannonball   (선택)
```

## 2. 프리뷰 Actor 만들기

1. Content Browser에서 Blueprint Class를 만든다.
2. All Classes에서 `BombardmentPreview`를 검색해 부모로 선택한다.
3. 이름을 `BP_BombardmentPreview`로 한다.
4. Components의 `PreviewMesh`를 선택한다.
5. 준비한 수평 원형 Static Mesh를 Static Mesh 슬롯에 넣는다.
6. 반투명 원형 머티리얼을 Material 0에 넣는다.

메시 조건:

- 중심 Pivot 권장
- 로컬 XY가 원의 평면이고 로컬 Z가 위쪽인 형태 권장
- 원본 크기는 자유롭다. C++이 Mesh Bounds를 읽어 SkillRadius에 맞춘다.
- Collision은 코드가 끄므로 에셋 Collision은 중요하지 않다.

머티리얼 권장:

- Blend Mode: Translucent 또는 Additive
- Shading Model: Unlit 권장
- 파란색 Emissive와 낮은 Opacity
- 물 위에서 잘 안 보이면 Depth Fade 또는 부드러운 외곽 Alpha 사용

`BP_BombardmentPreview` Class Defaults에서 선택 설정:

- `Valid Preview Material`: 유효 위치용. 비우면 PreviewMesh Material 0 사용
- `Invalid Preview Material`: 사거리 밖 등을 빨갛게 보이고 싶을 때 지정
- `Target Highlight Overlay Material`: 파란 Emissive Overlay Material 지정
- `Highlight Refresh Interval`: 기본 0.1초
- `Use Custom Depth Highlight`: Post Process Outline이 이미 준비된 경우만 활성화
- `Target Highlight Stencil Value`: 프로젝트 Post Process 규칙에 맞는 값

Overlay Material은 Static Mesh와 Skeletal Mesh 양쪽에서 사용할 수 있게 Usage 플래그를 확인한다. 저장 후 셰이더 컴파일 경고가 나오면 머티리얼의 Automatically Set Usage를 켜거나 필요한 Used With 옵션을 활성화한다.

## 3. Bombardment 실행 Actor 만들기

1. Blueprint Class를 만든다.
2. 부모로 `Bombardment`를 선택한다.
3. 이름을 `BP_Bombardment`로 한다.
4. Class Defaults에서 `Preview Class`를 `BP_BombardmentPreview`로 지정한다.

초기 테스트 추천값:

| 속성 | 추천 시작값 |
|---|---:|
| Skill Radius | 3000 cm |
| Max Target Range | 15000 cm |
| Preview Height Offset | 20 cm |
| Launch Height Z | 10000 cm |
| Launch XY Offset | X=6000, Y=0 cm |
| Projectiles Per Volley | 6 |
| Burst Spread Duration | 0.5 s |
| Volley Interval | 2.0 s |
| Volley Count | 3 |
| Distribution Mode | Stratified Disk |
| Distribution Jitter | 0.35 |
| Draw Debug | 첫 검증 때만 true |

`Launch XY Offset`은 월드 좌표다. 어느 방향에서 포탄이 날아올지 바꾸려면 X/Y를 조절한다.

예:

- `X=6000, Y=0`: 월드 +X 쪽 하늘에서 접근
- `X=0, Y=-6000`: 월드 -Y 쪽 하늘에서 접근
- `X=5000, Y=5000`: 대각선 방향에서 접근

## 4. 포탄 클래스 설정

### 권장: 기존 Player 배 대포알 자동 재사용

`BP_Bombardment`의 `Projectile Class Override`를 None으로 둔다.

그 후 Player Ship에 붙은 `BP_Cannon` 또는 `BP_Cannon` 계열 Actor에서 다음을 확인한다.

```text
Cannonball Class = /Game/New/Cannon/BP_CannonBall
```

Bombardment는 현재 배에 붙은 `ACannon`을 찾아 그 일반 Cannonball Class를 사용한다. 따라서 그 Blueprint의 Mesh, 머티리얼, Trail 등 기존 외형이 그대로 나온다. 데미지와 비행 속도는 포탄 Blueprint 기본값이 아니라 Player Ship의 현재 ASC 속성으로 초기화된다.

### 선택: 포탄 세례 전용 포탄 Blueprint

별도 외형이 필요하면 기존 `BP_CannonBall`의 Child Blueprint를 만들어 `BP_BombardmentCannonball`로 저장한다. Mesh/VFX만 변경하고 `BP_Bombardment`의 `Projectile Class Override`에 넣는다.

이 경우에도 런타임 Damage와 Speed는 Player Ship에서 가져온다.

주의: Override가 None인데 배에 유효한 `ACannon`/Cannonball Class가 없으면 서버 로그에 다음 오류가 나오고 포격이 생성되지 않는다.

```text
[Bombardment] No normal cannonball class was found ... Set ProjectileClassOverride.
```

## 5. Gameplay Ability 만들기

1. Blueprint Class를 만든다.
2. 부모로 `GA_Bombardment`를 선택한다.
3. 이름을 `GA_Bombardment`로 한다.
4. Class Defaults에서 `Bombardment Class`를 `BP_Bombardment`로 지정한다.

Player Blueprint를 연다. 현재 프로젝트에서는 실제 플레이어로 사용하는 `ABasePlayer` 파생 Blueprint를 선택한다.

Class Defaults의 다음 값을 확인한다.

```text
Abilities | Bombardment
  Grant Bombardment Ability = true
  Bombardment Ability Class = GA_Bombardment
```

설정하지 않아도 C++ 기본값은 Native `UGA_Bombardment`를 부여하지만, 에디터에서 만든 `BP_Bombardment` 튜닝을 사용하려면 반드시 `GA_Bombardment` Blueprint를 Player의 Bombardment Ability Class에 넣어야 한다.

## 6. 맵과 충돌 설정 확인

Water:

- 레벨에 Water Body Ocean 등 정상 Water Body가 있어야 한다.
- Bombardment는 Water Collision 첫 Hit에 의존하지 않고 Water Plugin 위치 조회를 사용한다.
- 파도 높이는 의도적으로 무시한다.

Landscape 섬:

- 섬 Actor가 `Landscape`/`LandscapeStreamingProxy` 계열이어야 한다.
- Landscape Collision이 Query 가능하고 Object Type이 기본 `WorldStatic`이어야 한다.
- 수면 아래 Landscape 지형은 섬으로 선택하지 않고 바다 표면을 선택한다.

적 하이라이트:

- 적 배는 `IsEnemyShipForEffects()`가 true여야 한다.
- 일반 적은 `IsEnemyCharacterForEffects()`가 true여야 한다.
- Overlay Material을 지정하지 않고 CustomDepth도 끄면 하이라이트는 보이지 않는다.

## 7. 1인 PIE 테스트 순서

1. Editor를 C++ 빌드 후 재시작한다.
2. Player, Player Ship, Player Ship에 붙은 대포, Water Body, Landscape 섬, 적 배를 배치한다.
3. PIE를 시작하고 배에 탑승해 PlayerController가 배를 Possess하게 한다.
4. 숫자 `5`를 누른다.

확인:

- 마우스 커서가 나타난다.
- 마우스를 움직여도 카메라가 회전하지 않는다.
- W/A/S/D로 배 운항은 계속 가능하다.
- 원형 Preview가 커서를 따라 움직인다.

5. 적 배가 커서 Ray 앞을 가리도록 조준한다.

확인:

- Preview가 적 배 갑판 높이로 올라가지 않고 뒤/아래의 바다 표면에 놓인다.
- 적 배가 Preview 원 안에 들어오면 파란 Overlay 또는 설정한 Stencil Highlight가 보인다.

6. Landscape 섬을 직접 가리킨다.

확인:

- Preview 중심이 해수면이 아니라 실제 Landscape Hit 높이로 올라간다.
- 좌클릭 확정이 허용된다.

7. 좌클릭한다.

확인:

- Preview와 하이라이트가 사라진다.
- `LaunchHeightZ` 위쪽과 `LaunchXYOffset` 방향의 원에서 포탄이 생성된다.
- 첫 묶음의 N발이 `BurstSpreadDuration` 동안 투두두둥 발사된다.
- 다음 묶음이 `VolleyInterval`의 시작 간격으로 나온다.
- 총 묶음 수가 `VolleyCount`와 일치한다.
- 포탄 위치가 원 전체에 비교적 고르게 퍼진다.
- 포탄이 바다 또는 Landscape 지정 도착점에서 멈추고 제거된다.
- 적 배 직격 시 기존 대포알 데미지가 적용된다.

8. 다시 `5`를 누르고 우클릭, Esc, `5` 재입력을 각각 시험한다.

확인:

- 모두 조준을 취소한다.
- 커서와 카메라 조작이 정상 복구된다.
- 하이라이트가 남지 않는다.

9. 조준 중 하선한다.

확인:

- GA와 프리뷰가 강제로 정리된다.
- Player 캐릭터 조작으로 정상 복귀한다.

## 8. 멀티플레이 PIE 테스트

PIE 설정:

```text
Number of Players = 2
Net Mode = Play As Listen Server
Run Under One Process = 필요에 따라 선택
```

각 클라이언트에서 확인한다.

- 조준 커서, Preview, 적 하이라이트는 사용하는 로컬 플레이어에게만 보인다.
- 상대 클라이언트는 다른 플레이어의 Preview를 보지 않는다.
- 확정된 포탄은 서버에서 생성되어 두 플레이어에게 보인다.
- 클라이언트가 보낸 Z가 아니라 서버가 Water/Landscape Z를 재계산한다.
- 다른 Pawn을 조종하거나 배에 타지 않은 상태에서는 숫자 5로 Bombardment를 사용할 수 없다.

## 9. 자동화 테스트 재실행

Session Frontend에서 다음 Prefix를 실행할 수 있다.

```text
ArtisticSW.Bombardment
```

명령줄 예:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject' `
  -ExecCmds='Automation RunTests ArtisticSW.Bombardment;Quit' `
  -unattended -nop4 -nosplash -NullRHI `
  -TestExit='Automation Test Queue Empty' -log
```

2026-07-22 기준 6개 테스트가 모두 Success다.

## 10. 현재 후속 작업 목록

- 착탄 폭발 VFX/SFX
- 일반 적과 적 배 대상의 착탄 범위 피해
- 폭발 반경/감쇠/친선 피해 정책
- GA Cost/Cooldown 적용
- 숫자 5 직접 바인딩을 최종 Enhanced Input Action으로 교체
- Static Mesh 섬도 Landscape와 같은 표면으로 허용할지 결정
- 대규모 맵에서 Water Body 캐시 및 하이라이트 Spatial Query 최적화
