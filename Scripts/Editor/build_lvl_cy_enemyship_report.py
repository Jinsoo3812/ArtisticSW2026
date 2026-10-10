"""Build placement instructions and a transfer manifest from read-only UE exports."""
import copy
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INPUT = ROOT / 'Saved/LvlCYExtraction'
OUTPUT = ROOT / 'Planning/EnemyShip/Lvl_CY_Backup_Extraction'


def load(variant):
    path = INPUT / f'{variant}_enemyships.json'
    if not path.exists():
        path = OUTPUT / f'{variant}_snapshot.json'
    return json.loads(path.read_text(encoding='utf-8'))


def sort_key(row):
    return [int(part) if part.isdigit() else part
            for part in re.split(r'(\d+)', row['actor_label'])]


def asset_name(value):
    if not value:
        return 'None'
    return value.get('asset_path', '').split('.')[-1]


def bp_name(row):
    return row['class_path'].split('.')[-1].removesuffix('_C')


def bp_display(row):
    package = row['blueprint_path'].split('.')[0]
    return package.removeprefix('/Game/GameplayAbilitySystem/Enemy/Balancing/Final/')


def public_tags(row):
    return [tag for tag in row['properties'].get('tags', [])
            if not tag.startswith('SWRoomStableId=')]


def flatten(value, prefix=''):
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if key == 'struct_type' and 'fields' in value:
                continue
            result.update(flatten(child, f'{prefix}.{key}' if prefix else key))
        return result
    return {prefix: value}


def differences(before, after):
    old = flatten(before)
    new = flatten(after)
    return {key: {'before': old.get(key), 'after': new.get(key)}
            for key in sorted(set(old) | set(new)) if old.get(key) != new.get(key)}


def authored(row):
    if not row:
        return {}
    result = {'properties': copy.deepcopy(row['properties']),
              'blueprint_properties': row.get('blueprint_properties', {}),
              'components': {name: data['properties'] for name, data in row['components'].items()}}
    result['properties']['tags'] = public_tags(row)
    return result


def short_value(value):
    if value is None:
        return 'None'
    if isinstance(value, dict) and 'asset_path' in value:
        return asset_name(value)
    if isinstance(value, list):
        return ', '.join(str(item) for item in value) or '(없음)'
    return str(value)


def main():
    base, backup, current = (load(name) for name in ('base', 'backup', 'current'))
    for snapshot in (base, backup, current):
        assert snapshot['enemyship_count'] == len(snapshot['enemyships'])
        labels = [row['actor_label'] for row in snapshot['enemyships']]
        assert len(labels) == len(set(labels)), 'Ambiguous duplicate actor labels'
        assert all(not row['unreadable_properties'] for row in snapshot['enemyships'])
        assert all(not data['unreadable_properties'] for row in snapshot['enemyships']
                   for data in row['components'].values())
    base_by_label = {row['actor_label']: row for row in base['enemyships']}
    current_by_label = {row['actor_label']: row for row in current['enemyships']}
    backup_by_label = {row['actor_label']: row for row in backup['enemyships']}
    unmatched_current = [row for label, row in current_by_label.items() if label not in backup_by_label]
    targets = []
    for source in sorted(backup['enemyships'], key=sort_key):
        label = source['actor_label']
        old, now = base_by_label.get(label), current_by_label.get(label)
        status = '새 배치' if not now else ('BP 교체' if now['class_path'] != source['class_path'] else '기존 사용')
        targets.append({
            'actor_label': label, 'expected_class_path': source['class_path'],
            'blueprint_asset_path': source['blueprint_path'], 'placement_action': status,
            'current_actor': now, 'source_actor': source,
            'settings_for_transfer': authored(source),
            'base_to_backup_changes': differences(authored(old), authored(source)),
            'current_to_backup_changes': differences(authored(now), authored(source)),
            'class_changed_from_base': old is not None and old['class_path'] != source['class_path'],
            'preserve_target_transform': True, 'copy_room_stable_id': False,
        })
    status_counts = Counter(row['placement_action'] for row in targets)
    manifest = {
        'schema_version': 1, 'source_branch': 'lv-cy-backup',
        'source_commit': backup['commit'], 'target_branch': 'WonjunJang',
        'target_commit_at_extraction': current['commit'], 'base_commit': base['commit'],
        'map_path': '/Game/Level/Lvl_CY',
        'snapshot_counts': {name: data['enemyship_count'] for name, data in
                            (('base', base), ('backup', backup), ('current', current))},
        'placement_action_counts': dict(status_counts),
        'matching_policy': 'exact_unique_actor_label_and_exact_blueprint_class',
        'transform_policy': 'preserve_manually_placed_target_location_rotation_scale',
        'reserved_tag_policy': 'retain target SWRoomStableId; do not clone source identity',
        'source_values_include_blueprint_inherited_defaults': True,
        'unmatched_current_actors': unmatched_current,
        'targets': targets,
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'EnemyShip_Transfer.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    for name, data in (('backup', backup), ('base', base), ('current', current)):
        (OUTPUT / f'{name}_snapshot.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    lines = [
        '# Lvl_CY EnemyShip 배치 안내', '',
        f"백업 `lv-cy-backup` (`{backup['commit'][:8]}`), 작업 전 `{base['commit'][:8]}`, 현재 `WonjunJang` (`{current['commit'][:8]}`) 레벨을 읽어서 비교한 결과입니다.", '',
        f"EnemyShip은 작업 전 {base['enemyship_count']}척, 백업 {backup['enemyship_count']}척, 현재 {current['enemyship_count']}척입니다.",
        f"현재 기준: BP 교체 {status_counts['BP 교체']}척, 새 배치 {status_counts['새 배치']}척, 기존 BP 사용 {status_counts['기존 사용']}척.", '',
        '## 에디터에서 할 작업', '',
        '1. 현재 WonjunJang의 원본 프로젝트에서 `/Game/Level/Lvl_CY`를 엽니다. Saved 아래 검사 프로젝트를 열지 않습니다.',
        '2. 표의 Blueprint를 콘텐츠 브라우저에서 찾아 배치합니다. BP 교체 항목은 기존 BP_EnemyShip을 그대로 두는 방식으로 완료되지 않습니다.',
        '3. World Outliner 이름을 표의 액터 이름과 정확히 맞춥니다. 같은 이름의 기존 액터는 먼저 별도 이름으로 바꾸거나 교체하여 중복을 피합니다.',
        '4. 위치·회전·크기는 원하는 상태로 배치합니다. 백업 좌표는 참고값이며 자동 적용 시 현재 배치를 유지합니다.',
        '5. 기존 액터를 교체할 때 다른 액터/레벨 블루프린트의 참조가 있으면 새 액터로 다시 연결합니다. 새 액터 확인이 끝나면 교체한 구형 액터를 제거하여 중복 배치를 남기지 않습니다.',
        '6. 아래 별도 확인 항목까지 처리한 뒤 저장하고 에디터를 닫습니다. 이후 설정 적용 전 이름·BP 종류를 다시 검사합니다.', '',
        'Squad ID, Archetype, 태그, 상자 설정 및 검사 대상 컴포넌트 설정은 JSON에 보관했습니다. 이번 단계에서 전부 수동 입력할 필요는 없습니다.',
        '`SWRoomStableId=...` 태그는 저장 시스템의 액터 식별값이므로 복사하지 않습니다. 액터 이름 대신 쓰는 값도 아닙니다.',
        '비교에는 해당 BP에서 상속받은 기본 설정의 차이도 포함됩니다. 같은 BP 종류를 먼저 맞춘 뒤 인스턴스 설정을 적용해야 합니다.', '',
        '## 배치 목록', '',
        'Blueprint 열은 `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/` 아래의 상대 경로입니다. N1과 N2에 같은 파일 이름의 BP가 있으므로 폴더까지 확인합니다.', '',
        '| 처리 | World Outliner 액터 이름 | 배치할 Blueprint (Final 기준 경로) | Squad ID | Archetype | Actor Tags (식별값 제외) |',
        '|---|---|---|---|---|---|',
    ]
    for target in targets:
        row = target['source_actor']
        prop = row['properties']
        tags = ', '.join(public_tags(row))
        lines.append(f"| {target['placement_action']} | `{row['actor_label']}` | `{bp_display(row)}` | `{prop['squad_id']}` | `{asset_name(prop['enemy_ship_archetype'])}` | {tags} |")
    lines += ['', '## 현재에만 있는 EnemyShip', '']
    if unmatched_current:
        for row in unmatched_current:
            lines.append(f"- `{row['actor_label']}` — 현재 BP: `{bp_name(row)}`. 백업 목록에는 없으므로 중복/불필요한 액터인지 확인하고 처리합니다.")
        if 'BP_ES_Normal_3_10' in current_by_label and 'BP_ES_Mid_3' in backup_by_label:
            lines += ['', '백업 구성에는 `BP_ES_Normal_3_10`이 없고 `BP_ES_Mid_3`가 있습니다. 백업과 같은 구성을 만들려면 기존 Normal_3_10을 남긴 채 Mid_3를 추가하여 총 52척이 되지 않도록 처리합니다.']
    else:
        lines.append('없음.')
    lines += ['', '## Blueprint 위치', '', '| Blueprint | 콘텐츠 브라우저 경로 | 수량 |', '|---|---|---|']
    classes = Counter(row['source_actor']['blueprint_path'] for row in targets)
    for path, count in sorted(classes.items()):
        name = path.split('.')[-1]
        package = path.split('.')[0]
        lines.append(f'| `{name}` | `{package}` | {count} |')
    lines += ['', '## 저장된 자료', '',
              '- `EnemyShip_Transfer.json`: 액터별 대상 BP, 전체 추출 설정, 변경 전/후 값, 현재 액터 정보. 자동 적용의 입력 자료.',
              '- `EnemyShip_Settings_Detail.md`: 액터별 핵심 설정, 변경 항목, 백업 좌표.',
              '- `backup_snapshot.json`, `base_snapshot.json`, `current_snapshot.json`: 읽기 전용 추출 원본.', '',
              '추출은 분리된 검사 프로젝트에서 수행했으며 원본 레벨을 저장하지 않았습니다. 레벨 로드와 추출 성공을 확인했으며 플레이 테스트는 수행하지 않았습니다.',
              'BP 에셋 내부, 다른 액터, 머티리얼/물리 컴포넌트 전체를 복제하는 자료는 아닙니다. 범위는 EnemyShip/Ship 편집 속성과 갑판·보스·항법 컴포넌트의 편집 설정입니다.', '']
    (OUTPUT / 'Placement_Guide.md').write_text('\n'.join(lines), encoding='utf-8')
    detail = ['# EnemyShip 설정 상세', '', 'World Outliner 이름 기준입니다. 좌표는 백업의 참고값이며 자동 적용 대상이 아닙니다.', '']
    for target in targets:
        row = target['source_actor']
        detail += [f"## {row['actor_label']}", '',
                   f"- 처리: {target['placement_action']}",
                   f"- BP: `{row['blueprint_path']}`",
                   f"- Squad ID: `{row['properties']['squad_id']}`",
                   f"- Archetype: `{asset_name(row['properties']['enemy_ship_archetype'])}`",
                   f"- Actor Tags: {', '.join(public_tags(row))}",
                   f"- 백업 위치(cm): `{row['location']['export_text']}`",
                   f"- 백업 회전: `{row['rotation']['export_text']}`",
                   f"- 백업 크기: `{row['scale']['export_text']}`", '',
                   '핵심 설정:', '']
        selected = ['chest_spawn_point_chest_settings', 'chest_spawn_point_loot_settings',
                    'enable_distance_optimization', 'distance_optimization_range',
                    'override_cannon_lead_speed', 'cannon_lead_speed_override']
        for field in selected:
            value = row['properties'].get(field)
            text = value.get('export_text', '') if isinstance(value, dict) else short_value(value)
            detail.append(f'- `{field}`: `{text}`')
        for name, data in row['components'].items():
            if name not in ('BossEncounterComponent', 'DeckEnemySpawnerComponent'):
                continue
            for field, value in data['properties'].items():
                text = value.get('export_text', '') if isinstance(value, dict) and 'export_text' in value else short_value(value)
                detail.append(f'- `{name}.{field}`: `{text}`')
        delta = target['base_to_backup_changes']
        detail += ['', f"작업 전 대비 설정 차이 {len(delta)}항목 (BP 상속 기본값 차이 포함):", '',
                   '| 항목 | 변경 전 | 백업 값 |', '|---|---|---|']
        for field, values in delta.items():
            before = short_value(values['before']).replace('|', '\\|')
            after = short_value(values['after']).replace('|', '\\|')
            detail.append(f'| `{field}` | {before} | {after} |')
        detail.append('')
    (OUTPUT / 'EnemyShip_Settings_Detail.md').write_text('\n'.join(detail), encoding='utf-8')
    print(json.dumps({'snapshot_counts': manifest['snapshot_counts'], 'placement_counts': dict(status_counts),
                      'unmatched_current_labels': [row['actor_label'] for row in unmatched_current],
                      'blueprint_counts': dict(classes), 'output': str(OUTPUT)}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
