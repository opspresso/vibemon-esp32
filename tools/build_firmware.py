#!/usr/bin/env python3
"""Build the RGB-safe ESP32-S3 SDK and firmware from pinned sources."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from sdk_toolchain import ROOT, IDF, load
from collect_licenses import collect


def configuration(base, overrides, arduino_kconfig):
    values = {}
    for line in overrides.splitlines():
        match = re.fullmatch(r'(?:# )?(CONFIG_[A-Za-z0-9_]+)(?:=(.*)| is not set)', line)
        if match:
            values[match[1]] = match[2] if match[2] is not None else 'n'
    used = {'Preferences', 'FS', 'Network', 'HTTPClient', 'WiFi', 'NetworkClientSecure'}
    for name in re.findall(r'^config ARDUINO_SELECTIVE_(\w+)$', arduino_kconfig, re.M):
        if name != 'COMPILATION':
            values['CONFIG_ARDUINO_SELECTIVE_' + name] = 'y' if name in used else 'n'
    lines = []
    for line in base.splitlines():
        match = re.match(r'(?:# )?(CONFIG_[A-Za-z0-9_]+)(?:=| is not set)', line)
        if not match or match[1] not in values:
            lines.append(line)
    lines.extend('# ' + key + ' is not set' if value == 'n' else key + '=' + value for key, value in values.items())
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    lock = json.loads((ROOT / 'toolchain.lock.json').read_text())
    subprocess.run([sys.executable, str(ROOT / 'tools/prepare_json.py')], check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/generate_contract.py')], check=True)
    environment, python = load(lock)
    for name, version in lock['libraries'].items():
        properties = ROOT / 'build/arduino/libraries' / name / 'library.properties'
        if not properties.exists() or ('version=' + version) not in properties.read_text().splitlines():
            parser.error('Run tools/setup.py to install ' + name + '@' + version)
    subprocess.run([sys.executable, str(ROOT / 'tools/generate_assets.py')], check=True)
    build = ROOT / 'build/firmware-idf'
    build.mkdir(exist_ok=True)
    config = configuration((ROOT / 'build/sdk-components/arduino.sdkconfig').read_text(),
                           (ROOT / 'firmware/sdkconfig.defaults').read_text(),
                           (ROOT / 'build/sdk-components/arduino/Kconfig.projbuild').read_text())
    (build / 'sdkconfig').write_text(config)
    subprocess.run([python, str(IDF / 'tools/idf.py'), '-C', str(ROOT / 'firmware'), '-B', str(build),
                    '-D', 'SDKCONFIG=' + str(build / 'sdkconfig'), 'reconfigure'], env=environment, check=True)
    sdkconfig = (build / 'config/sdkconfig.h').read_text()
    for setting in ['#define CONFIG_SPIRAM_MODE_OCT 1', '#define CONFIG_ESP32S3_DATA_CACHE_SIZE 0x10000',
                    '#define CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE 64', '#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1',
                    '#define CONFIG_SPIRAM_RODATA 1', '#define CONFIG_LCD_RGB_ISR_IRAM_SAFE 1']:
        if setting not in sdkconfig:
            raise ValueError('Required RGB SDK setting is missing: ' + setting)
    if '#define CONFIG_LCD_RGB_RESTART_IN_VSYNC 1' in sdkconfig:
        raise ValueError('Unconditional RGB DMA restart must be disabled')
    subprocess.run(['cmake', '--build', str(build), '--parallel', str(args.jobs)], env=environment, check=True)
    output = ROOT / 'build/firmware'
    output.mkdir(exist_ok=True)
    images = {}
    files = [('vibemon-esp32.ino.bootloader.bin', 'bootloader/bootloader.bin', 0),
             ('vibemon-esp32.ino.partitions.bin', 'partition_table/partition-table.bin', 0x8000),
             ('boot_app0.bin', 'ota_data_initial.bin', 0xe000), ('vibemon-esp32.ino.bin', 'vibemon_touch.bin', 0x10000)]
    for destination, source, offset in files:
        data = (build / source).read_bytes()
        if not data or (offset < 0xe000 and offset + len(data) > 0x9000) or offset + len(data) > 0x710000:
            raise ValueError('Firmware image exceeds its partition or overlaps NVS')
        (output / destination).write_bytes(data)
        images[destination] = {'offset': offset, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    for source, destination in [(ROOT / 'LICENSE', 'LICENSE'), (ROOT / 'assets/OFL-NanumGothic.txt', 'OFL-NanumGothic.txt'),
                                (IDF / 'LICENSE', 'LICENSE-ESP-IDF'), (ROOT / 'build/sdk-components/arduino/LICENSE.md', 'LICENSE-Arduino')]:
        shutil.copyfile(source, output / destination)
    binary = images['vibemon-esp32.ino.bin']
    description = json.loads((build / 'project_description.json').read_text())
    notices = collect(ROOT, description, lock)
    manifest = {'board': 'ESP32-4848S040', 'version': description['project_version'], 'framework': 'esp-idf', 'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
                'sha256': binary['sha256'], 'firmwareBytes': binary['size'], 'images': images, 'toolchain': lock, 'notices': notices,
                'display': {'pixelClockHz': 16000000, 'dataCacheBytes': 65536, 'cacheLineBytes': 64,
                            'forcedVsyncRestart': False, 'psramInstructionsAndRodata': True, 'cacheSafeInterrupts': True}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # An old Arduino merged image would overwrite NVS and use the wrong SDK.
    (output / 'vibemon-esp32.ino.merged.bin').unlink(missing_ok=True)
    print('Built ESP32-4848S040 firmware:', binary['size'], 'bytes')


if __name__ == '__main__':
    main()
