import argparse
import subprocess
import threading
import sys
import glob
import os
import re
import shutil


INTERACTOR_SRC = "interactor.cpp"
BIN_PREFIX = "bpc_2026_"
LOGS_DIR = "logs"
DEFAULT_TEST_DIR = "public_blueprints"

def _bin(name):
    if sys.platform == "win32":
        return os.path.join(".", name + ".exe")
    return os.path.join(".", name)


INTERACTOR_BIN = _bin(BIN_PREFIX + "interactor")


def find_test_files(test_dir):
    files = glob.glob(os.path.join(test_dir, "*.in")) + glob.glob(os.path.join(test_dir, "*.txt"))
    return sorted(files)


def needs_recompile(source, binary):
    if not os.path.exists(binary):
        return True
    return os.path.getmtime(binary) < os.path.getmtime(source)


def _require_tool(*candidates, env_var=None):
    if env_var:
        val = os.environ.get(env_var)
        if val and shutil.which(val):
            return val
    for cmd in candidates:
        if shutil.which(cmd):
            return cmd
    print(f"Error: none of {list(candidates)} found on PATH.", file=sys.stderr)
    sys.exit(1)


def solution_command(sol_src):
    ext = os.path.splitext(sol_src)[1].lower()
    name = os.path.basename(sol_src).replace(".", "_")

    if ext in (".cpp", ".cc", ".cxx"):
        bin_path = _bin(BIN_PREFIX + name)
        if needs_recompile(sol_src, bin_path):
            print("Compiling solution...", flush=True)
            cxx = _require_tool("g++", "clang++", "c++", env_var="CXX")
            subprocess.run([cxx, "-std=c++17", "-O2", "-o", bin_path, sol_src], check=True)
        return [bin_path]
    if ext == ".c":
        bin_path = _bin(BIN_PREFIX + name)
        if needs_recompile(sol_src, bin_path):
            print("Compiling solution...", flush=True)
            cc = _require_tool("gcc", "clang", "cc", env_var="CC")
            subprocess.run([cc, "-std=c11", "-O2", "-o", bin_path, sol_src], check=True)
        return [bin_path]
    if ext == ".py":
        return [sys.executable, "-u", sol_src]
    if ext == ".rs":
        bin_path = _bin(BIN_PREFIX + name)
        if needs_recompile(sol_src, bin_path):
            print("Compiling solution...", flush=True)
            rustc = _require_tool("rustc")
            subprocess.run([rustc, "-O", "-o", bin_path, sol_src], check=True)
        return [bin_path]
    if ext == ".java":
        bin_dir = os.path.join(".", BIN_PREFIX + name + "_classes")
        if needs_recompile(sol_src, bin_dir):
            os.makedirs(bin_dir, exist_ok=True)
            print("Compiling solution...", flush=True)
            javac = _require_tool("javac")
            subprocess.run([javac, "-d", bin_dir, sol_src], check=True)
        java = _require_tool("java")
        return [java, "-cp", bin_dir, java_main_class(sol_src)]

    supported = ".c, .cpp/.cc/.cxx, .py, .java, .rs"
    print(f"Unsupported file extension: {ext} (supported: {supported})")
    sys.exit(1)


def java_main_class(sol_src):
    with open(sol_src, encoding="utf-8") as f:
        source = f.read()

    package = re.search(r"^\s*package\s+([\w.]+)\s*;", source, re.MULTILINE)
    public_class = re.search(r"\bpublic\s+class\s+(\w+)\b", source)
    main_class = re.search(
        r"\bclass\s+(\w+)\b[\s\S]*?\bpublic\s+static\s+void\s+main\s*\(",
        source,
    )

    if public_class:
        class_name = public_class.group(1)
    elif main_class:
        class_name = main_class.group(1)
    else:
        class_name = os.path.splitext(os.path.basename(sol_src))[0]
    if package:
        return f"{package.group(1)}.{class_name}"
    return class_name


def compile_interactor():
    if not needs_recompile(INTERACTOR_SRC, INTERACTOR_BIN):
        return
    print("Compiling interactor...")
    cxx = _require_tool("g++", "clang++", "c++", env_var="CXX")
    subprocess.run(
        [cxx, "-std=c++17", "-O2", "-o", INTERACTOR_BIN, INTERACTOR_SRC],
        check=True,
    )


def forward(src, dst, name, log=None, scores=None):
    try:
        for line in iter(src.readline, ''):
            if not line:
                break
            if log:
                log.write(f"[{name}] {line}")
                log.flush()
            if "SCORE:" in line and scores is not None:
                match = re.search(r"SCORE: (\d+)", line)
                if match:
                    scores.append(int(match.group(1)))

            dst.write(line)
            dst.flush()
    except (BrokenPipeError, OSError):
        pass

    try:
        dst.close()
    except Exception:
        pass


def run_single_test(sol_cmd, test_file, log_path=None):
    log = None
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        log = open(log_path, "w", encoding="utf-8")

    # spawn solution
    sol = subprocess.Popen(
        sol_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    # spawn interactor
    inter = subprocess.Popen(
        [INTERACTOR_BIN, test_file],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1,
        **({"creationflags": 0} if sys.platform == "win32" else {}),
    )

    scores = []

    # thread 1: interactor -> solution
    t1 = threading.Thread(
        target=forward,
        args=(inter.stdout, sol.stdin, "INTERACTOR → SOLUTION", log, scores),
    )

    # thread 2: solution -> interactor
    t2 = threading.Thread(
        target=forward,
        args=(sol.stdout, inter.stdin, "SOLUTION → INTERACTOR", log, None),
    )

    t1.start()
    t2.start()

    inter.wait()
    sol.wait()

    t1.join()
    t2.join()

    if log:
        log.close()

    failed = False
    if inter.returncode != 0:
        print(f"Interactor exited with code {inter.returncode}", file=sys.stderr)
        failed = True
    if sol.returncode != 0:
        print(f"Solution exited with code {sol.returncode}", file=sys.stderr)
        failed = True

    if failed:
        return None

    return scores[0] if scores else 0

def cleanup_generated_files():
    for path in glob.glob(f"./{BIN_PREFIX}*"):
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
        else:
            try:
                os.remove(path)
            except FileNotFoundError:
                pass
    if os.path.isdir(LOGS_DIR):
        shutil.rmtree(LOGS_DIR, ignore_errors=True)


def main():
    if "--" in sys.argv:
        sep = sys.argv.index("--")
        script_args = sys.argv[1:sep]
        sol_cmd = sys.argv[sep + 1:]
        if not sol_cmd:
            print("Error: expected a command after '--'", file=sys.stderr)
            sys.exit(1)
    else:
        script_args = sys.argv[1:]
        sol_cmd = None

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command",
        nargs="?",
        choices=["clean"],
        help="remove all generated files",
    )
    parser.add_argument("--test-dir", default=None, help=argparse.SUPPRESS)
    parser.add_argument(
        "-t", "--test",
        default=None,
        help="Run a single test file instead of all tests in the directory."
    )
    parser.add_argument(
        "-s", "--solution",
        default="solution.cpp",
        help=(
            "Path to a .c, .cpp/.cc/.cxx, .py, .java, or .rs solution source "
            "file (default: solution.cpp)"
        ),
    )
    args = parser.parse_args(script_args)

    if args.command == "clean":
        cleanup_generated_files()
        print("Cleaned generated files.")
        return

    try:
        compile_interactor()

        if sol_cmd:
            sol_stem = "command"
            print(f"Running solution using command: {' '.join(sol_cmd)}\n", flush=True)
        else:
            sol_stem = os.path.basename(args.solution).replace(".", "_")
            sol_cmd = solution_command(args.solution)

        if args.test:
            test_files = [args.test]
        else:
            test_dir = args.test_dir or DEFAULT_TEST_DIR
            if not os.path.isdir(test_dir):
                print(
                    f"Test directory not found: {test_dir}/\n"
                    "Use --test to run a single file.",
                    file=sys.stderr,
                )
                sys.exit(1)
            test_files = find_test_files(test_dir)
            if not test_files:
                print(f"No test files found in {test_dir}/", file=sys.stderr)
                sys.exit(1)
            print(
                f"Running {len(test_files)} test(s) from {test_dir}/\n",
                flush=True,
            )

        total_score = 0
        any_failed = False

        for tf in test_files:
            name = os.path.basename(tf)
            print(f"===== {name} =====", flush=True)

            test_stem = os.path.splitext(name)[0]
            log_path = os.path.join(LOGS_DIR, f"{sol_stem}__{test_stem}.log")
            score = run_single_test(sol_cmd, tf, log_path=log_path)
            print(f"Interaction saved to {log_path}")

            if score is None:
                print("FAILED\n")
                any_failed = True
            else:
                print(f"SCORE: {score}\n")
                total_score += score

        print(f"{'=' * 40}")
        print(f"TOTAL SCORE: {total_score}")
        print(f"{'=' * 40}")

        if any_failed:
            sys.exit(1)

    except subprocess.CalledProcessError as e:
        print(f"Compilation failed (exit code {e.returncode}).", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
