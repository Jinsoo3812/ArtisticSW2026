"""Prepare compact cannonball plunges using the shared owned-water renderer."""
import argparse
import json
from pathlib import Path
from prepare_character_entry import prepare_character_entry

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--staging', type=Path, required=True)
    print(json.dumps(prepare_character_entry(p.parse_args().staging, cannon=True), indent=2))
