import argparse
import getpass
import posixpath
import re
import shlex
import sys
import tempfile
from pathlib import Path
import os
import shutil
import glob
import paramiko


def safe_name(path: Path) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", path.stem)


def remote_quote(value: str) -> str:
    return shlex.quote(value)


def run_remote(ssh: paramiko.SSHClient, command: str) -> tuple[int, str, str]:
    stdin, stdout, stderr = ssh.exec_command(f"bash -lc {remote_quote(command)}")
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    return stdout.channel.recv_exit_status(), out, err


def require_remote_ok(ssh: paramiko.SSHClient, command: str, label: str) -> str:
    code, out, err = run_remote(ssh, command)

    if code != 0:
        stdin, stdout, stderr = ssh.exec_command(command)
        out = stdout.read().decode()
        err = stderr.read().decode()
        code = stdout.channel.recv_exit_status()

        print("COMMAND:", command)
        print("EXIT:", code)
        print("OUT:", out)
        print("ERR:", err)
        raise RuntimeError(f"{label} failed with exit code {code}\n{err}")
    return out


def upload_text(sftp: paramiko.SFTPClient, text: str, remote_path: str) -> None:
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8", newline="\n") as handle:
        handle.write(text)
        local_temp = Path(handle.name)
    try:
        sftp.put(str(local_temp), remote_path)
        print(f"Put {str(local_temp)} to {remote_path}")
    finally:
        local_temp.unlink(missing_ok=True)


def repair_one_mesh(
    ssh: paramiko.SSHClient,
    sftp: paramiko.SFTPClient,
    args: argparse.Namespace,
    remote_home: str,
    input_path: Path,
) -> None:
    input_path = input_path.expanduser().resolve()
    
    print(repr(input_path))
    if not input_path.is_file():
        print(f"Missing file: {input_path}")
        return

    name = safe_name(input_path)
    remote_repo = args.remote_repo.replace("~", remote_home)
    remote_jobs = args.remote_jobs.replace("~", remote_home)
    remote_run_dir = posixpath.join(remote_jobs, name)
    remote_input_dir = posixpath.join(remote_run_dir, "input")
    remote_results_dir = posixpath.join(remote_run_dir, "results")
    remote_input = posixpath.join(remote_input_dir, f"{name}.obj")
    remote_config = posixpath.join(remote_run_dir, f"{name}.yaml")

    local_output_dir = args.output_dir.expanduser().resolve()
    local_output_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n[{name}] Preparing remote job folder")
    require_remote_ok(
        ssh,
        "\n".join(
            [
                "set -e",
                f"mkdir -p {remote_quote(remote_input_dir)} {remote_quote(remote_results_dir)}",
                f"test -d {remote_quote(remote_repo)}",
                f"test -f {args.remote_venv + '/bin/activate'}",
            ]
        ),
        "Remote setup",
    )

    config = "\n".join(
        [
            f"expname: {name}",
            f"objpath: {remote_input}",
            f"optimizer: {args.optimizer}",
            f"lr: {args.learning_rate}",
            f"savepath: {remote_results_dir}",
            f"max_collisions: {args.max_collisions}",
            f"num_iters: {args.num_iters}",
            f"energy: {args.energy}",
            "constraints:",
            "  - curvature",
            "",
        ]
    )

    print(f"[{name}] Uploading {input_path.name}")
    sftp.put(str(input_path), remote_input)
    upload_text(sftp, config, remote_config)

    print(f"[{name}] Running remote repair")
    commands = "\n".join(
            [
                "set -e",
                f"source {remote_quote(args.remote_venv + '/bin/activate')}",
                f"cd {remote_quote(remote_repo)}",
                f"python repair_factory.py --config {remote_quote(remote_config)}",
            ]
        )
    print(commands)
    code, out, err = run_remote(
        ssh,
        commands,
    )
    if out.strip():
        print(out.rstrip())
    if err.strip():
        print(err.rstrip(), file=sys.stderr)
    if code != 0:
        print(f"[{name}] Remote repair failed with exit code {code}")
        return

    print(f"[{name}] Downloading matching results")
    result_listing = require_remote_ok(
        ssh,
        f"find {remote_quote(remote_results_dir)} -maxdepth 1 -type f -name {remote_quote('*' + name + '*')} -print",
        "Result listing",
    )
    remote_files = [line.strip() for line in result_listing.splitlines() if line.strip()]
    if not remote_files:
        print(f"[{name}] No result files found")
        return

    for remote_file in remote_files:
        if "_best.obj" in remote_file:
            local_target = local_output_dir / posixpath.basename(remote_file)
            sftp.get(remote_file, str(local_target))
            print(f"[{name}] Downloaded {local_target}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Keep one SSH/SFTP session open and submit multiple OBJ repair jobs."
    )
    # cleared defaults
    parser.add_argument("--host", default="external.ceng.metu.edu.tr")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--inek-host", default="")
    parser.add_argument("--inek-port", type=int, default=0)
    parser.add_argument("--user", default="")
    parser.add_argument("--password", default="")
    parser.add_argument("--remote-repo", default="")
    parser.add_argument("--remote-venv", default="")
    parser.add_argument("--remote-jobs", default="")
    parser.add_argument("--output-dir", type=Path, default=Path("mesh_results"))
    parser.add_argument("--max-collisions", type=int, default=8)
    parser.add_argument("--num-iters", type=int, default=60)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--optimizer", default="GD")
    parser.add_argument("--energy", default="signed_TPE_verts")
    parser.add_argument("objs", nargs="*", type=Path, help="OBJ files to process immediately")
    return parser.parse_args()

import sys
def main() -> int:
    print("__START__", flush=True)
    sys.stdout.reconfigure(line_buffering=True)
    args = parse_args()
    
    dirs_to_remove = [ # insert any directories to remove beforehand here
        ""
    ]

    for path in dirs_to_remove:
        for target in glob.glob(path):
            if os.path.isdir(target):
                print(f"Removing {target}", flush=True)
                shutil.rmtree(target)
            elif os.path.isfile(target):
                print(f"Removing file {target}", flush=True)
                os.remove(target)
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    print(f"Opening SSH connection to jump host {args.host}:{args.port}")
    jump_ssh = paramiko.SSHClient()
    jump_ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump_ssh.connect(
        hostname=args.host,
        port=args.port,
        username=args.user,
        password=args.password,
        look_for_keys=False,
        allow_agent=False,
    )

    print(f"Opening tunneled SSH connection to {args.inek_host}:{args.inek_port}")
    jump_transport = jump_ssh.get_transport()
    if jump_transport is None:
        raise RuntimeError("Jump host SSH transport is not available.")

    inek_channel = jump_transport.open_channel(
        "direct-tcpip",
        (args.inek_host, args.inek_port),
        ("127.0.0.1", 0),
    )

    ssh.connect(
        hostname=args.inek_host,
        port=args.inek_port,
        username=args.user,
        password=args.password,
        sock=inek_channel,
        look_for_keys=False,
        allow_agent=False,
    )

    print("Opening SFTP channel on INEK host")
    sftp = ssh.open_sftp()

    try:
        remote_home = require_remote_ok(ssh, 'printf "%s" "$HOME"', "Remote HOME").strip()

        for obj in args.objs:
            repair_one_mesh(ssh, sftp, args, remote_home, obj)

        print("\nSession is open. Enter an OBJ path, or type q to quit.")
        while True:
            print("obj>", flush=True)
            value = sys.stdin.readline()
            if not value:
                continue
            value = value.strip()
            if value.lower() in {"q", "quit", "exit"}:
                break
            repair_one_mesh(ssh, sftp, args, remote_home, Path(value))
    finally:
        print("Closing SFTP and SSH")
        sftp.close()
        ssh.close()
        jump_ssh.close()
    print("__PYTHON_EXITING__", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
