"""Windows integration tests against the installed, unmodified Git LFS.

Only disposable repositories under --output are staged/committed/restored.
The interrupted-transfer tests kill only child processes created by this script.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--adapter", required=True)
parser.add_argument("--native-lfs", required=True)
parser.add_argument("--git", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
base = pathlib.Path(tempfile.mkdtemp(prefix="lfs-diff-test-", dir=args.output))
adapter = pathlib.Path(args.adapter).resolve()
native = pathlib.Path(args.native_lfs).resolve()
git_exe = pathlib.Path(args.git).resolve()
adapter_args = [str(adapter), "--native", str(native), "--git", str(git_exe)]
def shquote(value):
    return "'" + str(value).replace("'", "'\\''") + "'"
filter_command = " ".join(shquote(value) for value in adapter_args)
environment = dict(os.environ, GIT_OPTIONAL_LOCKS="0", CODEX_LFS_DIFF_TRACE="1")
checks = []

def run(repo, *options, expected=0, env=environment, input_bytes=None):
    child = subprocess.Popen([str(git_exe), "-C", str(repo), *options], stdin=subprocess.PIPE if input_bytes is not None else None, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        output, errors = child.communicate(input=input_bytes, timeout=45)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
        output, errors = child.communicate(timeout=10)
        raise AssertionError((str(repo), options, "timed out", output[-1000:], errors[-3000:]))
    result = subprocess.CompletedProcess(child.args, child.returncode, output, errors)
    if result.returncode != expected:
        raise AssertionError((str(repo), options, result.returncode, result.stdout[-1000:], result.stderr[-3000:]))
    return result

def state(repo):
    lfs = repo / ".git/lfs"
    return {
        "objects": sorted(str(p.relative_to(lfs)) for p in (lfs / "objects").glob("*/*/*") if p.is_file()),
        "temporary": sorted((p.name, p.stat().st_size) for p in (lfs / "tmp").glob("*") if p.is_file()),
    }

def create(name, use_adapter):
    repo = base / name
    repo.mkdir()
    run(repo, "init", "--template=")
    run(repo, "config", "core.fsmonitor", "false")
    if use_adapter:
        run(repo, "config", "filter.lfs.process", filter_command)
    (repo / ".gitattributes").write_text("*.bin filter=lfs diff=lfs merge=lfs -text\n")
    (repo / "asset.bin").write_bytes(b"baseline asset\0" * 32768)
    (repo / "small.bin").write_bytes(b"baseline")
    (repo / "empty.bin").write_bytes(b"")
    (repo / "유니코드 sample.bin").write_bytes(b"baseline unicode")
    add = run(repo, "-c", "test.argument=diff", "add", "--all")
    if use_adapter:
        assert b"lfs-diff-filter: native" in add.stderr, add.stderr
        assert b"hash-only" not in add.stderr, add.stderr
    run(repo, "-c", "user.name=Local Filter Test", "-c", "user.email=local-filter-test@invalid", "commit", "-m", "fixture")
    return repo

stock = create("stock", False)
subject = create("subject", True)
initial = state(subject)
payload = b"\xc1\x83\x2a\x9e" + b"modified asset data\0" * 500000
cases = [
    ("large", payload),
    ("empty", b""),
    ("small", b"small changed value"),
    ("boundary1023", b"x" * 1023),
    ("boundary1024", b"x" * 1024),
    ("boundary1025", b"x" * 1025),
    ("canonical-pointer", b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"a" * 64 + b"\nsize 9\n"),
    ("crlf-pointer", b"version https://git-lfs.github.com/spec/v1\r\noid sha256:" + b"b" * 64 + b"\r\nsize 10\r\n"),
]
for name, content in cases:
    for repo in (stock, subject):
        (repo / "asset.bin").write_bytes(content)
        (repo / "small.bin").write_bytes(b"changed small")
        (repo / "유니코드 sample.bin").write_bytes(b"unicode edit")
    reference = run(stock, "diff", "--no-ext-diff", "--no-textconv", "--binary")
    result = run(subject, "diff", "--no-ext-diff", "--no-textconv", "--binary")
    assert result.stdout == reference.stdout, name
    assert b"lfs-diff-filter: hash-only" in result.stderr, (name, result.stderr)
    assert state(subject) == initial, (name, state(subject), initial)
    checks.append("native-equivalent-diff-without-lfs-writes:" + name)
    print("passed " + checks[-1], flush=True)

# Actual desktop summary flags and simultaneous readers must not materialize data.
(subject / "asset.bin").write_bytes(payload)
head = run(subject, "rev-parse", "HEAD").stdout.decode().strip()
summary_flags = ["-c", "core.hooksPath=NUL", "-c", "core.fsmonitor=", "diff", "--no-ext-diff", "--no-textconv", "--color=never", "--src-prefix=a/", "--dst-prefix=b/", head, "--find-renames", "--raw", "--no-abbrev", "--numstat", "-z"]
readers = [subprocess.Popen([str(git_exe), "-C", str(subject), *summary_flags], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment) for _ in range(3)]
for reader in readers:
    output, errors = reader.communicate(timeout=45)
    assert reader.returncode == 0 and b"hash-only" in errors, (output, errors)
assert state(subject) == initial
checks.append("three-concurrent-desktop-comparisons-without-lfs-writes")

# Desktop also hashes untracked paths. -w is a write and must use native LFS.
hash_payload = b"new object for hash-object tests\0" * 100000
before_hash = state(subject)
for repo in (stock, subject):
    (repo / "asset.bin").write_bytes(hash_payload)
stock_hash = run(stock, "hash-object", "--stdin-paths", input_bytes=b"asset.bin\n")
for _ in range(5):
    read_hash = run(subject, "hash-object", "--stdin-paths", input_bytes=b"asset.bin\n")
    assert read_hash.stdout == stock_hash.stdout and b"hash-only" in read_hash.stderr, (read_hash.stdout, stock_hash.stdout, read_hash.stderr)
    assert state(subject) == before_hash
checks.append("read-only-hash-object-stdin-paths-without-lfs-writes")
write_hash = run(subject, "hash-object", "-w", "--stdin-paths", input_bytes=b"asset.bin\n")
assert write_hash.stdout == stock_hash.stdout and b"lfs-diff-filter: native" in write_hash.stderr
new_oid = hashlib.sha256(hash_payload).hexdigest()
assert (subject / ".git/lfs/objects" / new_oid[:2] / new_oid[2:4] / new_oid).read_bytes() == hash_payload
checks.append("writing-hash-object-uses-native-lfs")
stock_stdin = run(stock, "hash-object", "--path=asset.bin", "--stdin", input_bytes=b"independent stdin")
subject_stdin = run(subject, "hash-object", "--path=asset.bin", "--stdin", input_bytes=b"independent stdin")
assert subject_stdin.stdout == stock_stdin.stdout and b"lfs-diff-filter: native" in subject_stdin.stderr
checks.append("arbitrary-stdin-hash-object-preserves-native-semantics")

# A custom diff driver must retain original native behavior, even with no-textconv.
run(subject, "config", "diff.lfs.textconv", "git-lfs smudge")
fallback = run(subject, "diff", "--no-ext-diff", "--no-textconv", "--numstat")
assert b"lfs-diff-filter: native" in fallback.stderr, fallback.stderr
run(subject, "config", "--unset", "diff.lfs.textconv")
checks.append("custom-driver-falls-back-to-native")

# Add/commit/restore must continue storing and recovering the actual binary.
(subject / "asset.bin").write_bytes(payload)
add = run(subject, "-c", "test.argument=diff", "add", "--all")
assert b"lfs-diff-filter: native" in add.stderr and b"hash-only" not in add.stderr
run(subject, "-c", "user.name=Local Filter Test", "-c", "user.email=local-filter-test@invalid", "commit", "-m", "fixture changed asset")
(subject / "asset.bin").write_bytes(b"uncommitted change for restore test")
restore = run(subject, "restore", "--worktree", "--", "asset.bin")
assert b"lfs-diff-filter: native" in restore.stderr, restore.stderr
assert (subject / "asset.bin").read_bytes() == payload
assert not run(subject, "status", "--porcelain=v1").stdout
for obj in (subject / ".git/lfs/objects").glob("*/*/*"):
    assert hashlib.sha256(obj.read_bytes()).hexdigest() == obj.name
checks.append("native-add-commit-smudge-restore-and-object-integrity")

def packet(data):
    return f"{len(data)+4:04x}".encode() + data
def read_packet(stream):
    header = stream.read(4)
    if len(header) != 4:
        raise AssertionError((header, "missing packet header"))
    size = int(header, 16)
    return b"" if size == 0 else stream.read(size - 4)
def read_headers(stream):
    result = []
    while True:
        part = read_packet(stream)
        if not part:
            return result
        result.append(part)

# Bounded forced cancellation of the same streaming handler: no file to orphan.
before_cancel = state(subject)
child = subprocess.Popen([*adapter_args, "--test-hash-only"], cwd=subject, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
child.stdin.write(packet(b"git-filter-client\n") + packet(b"version=2\n") + b"0000")
child.stdin.flush()
assert b"git-filter-server\n" in read_headers(child.stdout)
child.stdin.write(packet(b"capability=clean\n") + packet(b"capability=smudge\n") + b"0000")
child.stdin.flush()
assert b"capability=clean\n" in read_headers(child.stdout)
child.stdin.write(packet(b"command=clean\n") + packet(b"pathname=asset.bin\n") + b"0000")
block = b"q" * 60000
for _ in range(100):
    child.stdin.write(packet(block))
child.stdin.flush()
child.kill()
child.wait(timeout=10)
child.stdin.close()
child.stdout.close()
child.stderr.close()
assert state(subject) == before_cancel
checks.append("terminated-mid-content-without-orphan-temporary-file")

report = {"passed": len(checks), "checks": checks, "fixtureRoot": str(base), "adapterSha256": hashlib.sha256(adapter.read_bytes()).hexdigest(), "sourceSha256": hashlib.sha256((pathlib.Path(__file__).resolve().parents[1] / "LfsDiffFilter.cs").read_bytes()).hexdigest()}
path = pathlib.Path(args.output) / "lfs-diff-filter-test-report.json"
path.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report))
