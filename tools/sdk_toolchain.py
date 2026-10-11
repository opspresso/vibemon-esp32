"""Prepare an isolated ESP-IDF build while preserving installed Arduino packages."""
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
IDF = ROOT / "build/esp-idf"
TOOLS = ROOT / "build/idf-tools"
STATE = ROOT / "build/sdk-toolchain.json"


def base_environment():
    environment = os.environ.copy()
    environment.update(IDF_PATH=str(IDF), IDF_TOOLS_PATH=str(TOOLS), IDF_COMPONENT_MANAGER="0")
    return environment


def sdk_environment(python):
    environment = base_environment()
    output = subprocess.check_output([python, str(IDF / "tools/idf_tools.py"), "export", "--format", "key-value"], env=environment, text=True)
    for line in output.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            environment[key] = value.replace("$PATH", environment["PATH"])
    executable = Path(environment["IDF_PYTHON_ENV_PATH"]) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    return environment, str(executable)


def install(lock, arduino, sdk, python):
    python = shutil.which(python) or str(Path(python).resolve())
    if not Path(python).is_file():
        raise ValueError("The ESP-IDF bootstrap Python executable was not found")
    if not (arduino / "CMakeLists.txt").is_file() or not (sdk / "sdkconfig").is_file():
        raise ValueError("The pinned Arduino source package and SDK configuration are required")
    components = ROOT / "build/sdk-components"
    component = components / "arduino"
    component.mkdir(parents=True, exist_ok=True)
    # The installed package stays untouched. ESP-IDF resolves this stable component name.
    for name in ("cores", "libraries"):
        shutil.copytree(arduino / name, component / name, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("examples", "tests", ".git", "__pycache__"))
    shutil.copytree(arduino / "variants/esp32s3", component / "variants/esp32s3", dirs_exist_ok=True)
    for name in ("CMakeLists.txt", "Kconfig.projbuild"):
        shutil.copyfile(arduino / name, component / name)
    license_file = component / "LICENSE.md"
    if not license_file.exists() or hashlib.sha256(license_file.read_bytes()).hexdigest() != lock["arduino_license_sha256"]:
        url = "https://raw.githubusercontent.com/espressif/arduino-esp32/" + lock["esp32_core"] + "/LICENSE.md"
        with urllib.request.urlopen(url, timeout=30) as response:
            license_data = response.read(100000)
        if hashlib.sha256(license_data).hexdigest() != lock["arduino_license_sha256"]:
            raise ValueError("Arduino license content does not match the pinned source")
        license_file.write_bytes(license_data)
    shutil.copyfile(sdk / "sdkconfig", components / "arduino.sdkconfig")
    if not IDF.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", "v" + lock["esp_idf"]["version"],
                        "--recursive", "--shallow-submodules", "https://github.com/espressif/esp-idf.git", str(IDF)], check=True)
    commit = subprocess.check_output(["git", "-C", str(IDF), "rev-parse", "HEAD"], text=True).strip()
    if commit != lock["esp_idf"]["commit"]:
        raise ValueError("The ESP-IDF checkout does not match toolchain.lock.json")
    environment = base_environment()
    command = [python, str(IDF / "tools/idf_tools.py"), "--non-interactive"]
    subprocess.run(command + ["install", "--targets", "esp32s3"], env=environment, check=True)
    subprocess.run(command + ["install-python-env"], env=environment, check=True)
    _, executable = sdk_environment(python)
    STATE.write_text(json.dumps({"bootstrapPython": str(Path(python).resolve()), "python": executable, "toolchain": lock}, indent=2) + "\n")


def load(lock):
    if not STATE.exists():
        raise ValueError("Run tools/setup.py --firmware first")
    state = json.loads(STATE.read_text())
    if state["toolchain"] != lock:
        raise ValueError("The toolchain changed; run tools/setup.py --firmware again")
    commit = subprocess.check_output(["git", "-C", str(IDF), "rev-parse", "HEAD"], text=True).strip()
    if commit != lock["esp_idf"]["commit"]:
        raise ValueError("The ESP-IDF checkout does not match the lock file")
    if subprocess.check_output(["git", "-C", str(IDF), "status", "--porcelain", "--untracked-files=no"], text=True).strip():
        raise ValueError("Use an unmodified ESP-IDF checkout for firmware builds")
    environment, executable = sdk_environment(state["bootstrapPython"])
    return environment, executable
