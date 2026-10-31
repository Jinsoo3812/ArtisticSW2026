# 작업대 아이콘 첫 표시 수정

강화 노드의 아이콘이 검은 사각형으로 표시되고 제작 아이콘이 재갱신 이후에야 보이는 증상에 대한 수정이다.

## 확인한 원인

실제 `WBP_ShipUpgradeNode`를 로드하여 아이콘 데이터와 브러시를 검사했다. 트리에 지정된 텍스처는 정상 로드되고 브러시에 적용됐지만, `Image_Icon.ColorAndOpacity`가 `(0, 0, 0, 0.5)`였다. 기존 코드는 `SetOpacity(1)`만 호출하므로 검정 RGB가 남아 텍스처가 검게 출력된다. 변경 전 `ArtisticSW.ShipUpgrade.UI.NodeIcons` 테스트에서 모든 노드의 검정 곱셈 검사가 실패했다. 로그: `Saved/Logs/WorkbenchNodeIcons.log`.

아이템 공통 머티리얼 경로도 이미지 색상을 초기화하지 않았다. 또한 이전 `SetBrushFromTexture(..., true)`에서 수행되던 에디터 텍스처 컴파일 완료 대기와 UI용 mip 상주 설정을 건너뛰고 있었다. 공통 아이콘 적용 시 색상과 리소스 준비를 함께 보장한다. 텍스처 준비 누락은 코드상 확인한 차이이며, 제공된 화면에서 그 기여도를 별도로 측정한 것은 아니다.

## Implementation Log & Checklist

- [x] 강화 노드의 아이콘 상태 갱신에서 opacity만 변경하는 대신 흰색·불투명 색상 적용. 상태별 색상은 기존 노드 배경에 유지.
- [x] 공통 아이템 아이콘 적용 시 검정 placeholder 색상을 흰색·불투명으로 초기화. 기존 희귀도 머티리얼 선택과 이미지별 MID 재사용 유지.
- [x] 공통 머티리얼 경로에서 UImage의 텍스처 준비 정책 복원: WITH_EDITOR 텍스처 컴파일 완료, UI mip 상주/스트리밍 bias 제외.
- [x] 실제 강화 트리/위젯을 사용하는 검정 아이콘 회귀 테스트 추가. 실제 제작·스킬 재료 위젯에서 데이터 적용 후 Slate 구성/재갱신을 확인하는 테스트 추가.
- [x] 기존 MSVC 14.44로 ArtisticSW2026Editor Win64 Development 컴파일 및 최종 링크 성공.
- [x] 수정 후 관련 5개 자동화 테스트 전부 통과: NodeIcons, WorkbenchFirstDisplay, InventoryWidgetIntegration, MaterialIsolation, TextureFallback. 실제 제작·스킬 재료 위젯도 초기 `(0,0,0,0.5)` 색상이었음을 확인했다. 최종 로그: `Saved/Logs/WorkbenchIcons_Final.log`.
- [x] 인벤토리 테스트가 이미 정상화된 퀘스트 데이터에서 과거의 invalid-tag 경고 3개를 요구하던 낡은 기대값 제거. 실제 아이콘/희귀도/비우기 검사는 유지.
- [ ] 사용자의 Standalone `-game` 화면에서 최종 육안 확인은 수행하지 않음.

에셋 및 게임플레이·네트워크 코드는 수정하지 않았다. 기존 날씨/점프 작업 변경은 유지했다.
