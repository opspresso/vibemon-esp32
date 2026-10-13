"""Retain source-package notices and compiled-source attributions in firmware artifacts."""
import hashlib
import json
from pathlib import Path
import re

NOTICE = re.compile(r"(?:licen[sc]e|copying|copyright|notice|ofl)", re.IGNORECASE)
EXCLUDED = {'.git', 'tests', 'test', 'test_apps', 'testsuite', 'examples', 'tools', 'scripts', 'build'}
LEADING_COMMENTS = re.compile(rb"\s*(?:(?:/\*.*?\*/|//[^\n]*(?:\n|$))\s*)*", re.DOTALL)


def notice_files(root):
    """Skip test/example licenses, but retain library-specific nested notices."""
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if not path.is_file() or set(relative.parts[:-1]) & EXCLUDED:
            continue
        if NOTICE.search(path.name) and path.suffix.lower() not in {'.py', '.js', '.cmake', '.c', '.h', '.mdx'}:
            if not path.resolve().is_relative_to(root.resolve()):
                raise ValueError('Notice escapes its source package: ' + str(relative))
            yield path


def write_notices(output, payload, components):
    """Only replace files recorded by this generator; preserve unrelated output."""
    if output.is_symlink():
        raise ValueError('Notice output must not be a symlink')
    output.mkdir(parents=True, exist_ok=True)
    index_file = output / 'index.json'
    previous = json.loads(index_file.read_text()) if index_file.exists() else {'files': {}}
    for name in [*previous['files'], *payload]:
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or any(ord(char) < 32 for char in name) or path.as_posix() in {'', '.', 'index.json'}:
            raise ValueError('Invalid notice output path')
        if not (output / path).resolve().is_relative_to(output.resolve()):
            raise ValueError('Notice output escapes its directory')
        if any(parent.is_symlink() for parent in [output / path, *(output / path).parents] if parent.is_relative_to(output)):
            raise ValueError('Notice output must not contain symlinks')
    actual = {path.relative_to(output).as_posix() for path in output.rglob('*') if path.is_file()}
    if actual - set(previous['files']) - {'index.json'}:
        raise ValueError('Preserve unrelated files in the generated notices directory')
    for name, content in sorted(payload.items()):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    for name in set(previous['files']) - set(payload):
        (output / name).unlink(missing_ok=True)
    files = {name: hashlib.sha256(content).hexdigest() for name, content in sorted(payload.items())}
    index = {'scope': 'Pinned source components and toolchain runtime distributions. Link-time elimination may omit code from a listed component.',
             'components': components, 'files': files}
    index_file.write_text(json.dumps(index, indent=2) + '\n')
    return {'index': 'licenses/index.json', 'sha256': hashlib.sha256(index_file.read_bytes()).hexdigest(), 'files': len(files)}


def verify_notices(output, expected_hash):
    index_file = output / 'index.json'
    if hashlib.sha256(index_file.read_bytes()).hexdigest() != expected_hash:
        raise ValueError('Notice index hash mismatch')
    index = json.loads(index_file.read_text())
    actual = {path.relative_to(output).as_posix() for path in output.rglob('*') if path.is_file()}
    if actual != set(index['files']) | {'index.json'}:
        raise ValueError('Unexpected notice artifact content')
    for name, digest in index['files'].items():
        path = output / name
        if not path.resolve().is_relative_to(output.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Notice hash mismatch: ' + name)


def collect(root, description, lock):
    root = root.resolve()
    payload, components = {}, {}
    def add(package, base, path):
        if not path.is_file() or not path.resolve().is_relative_to(base.resolve()):
            raise ValueError('Missing or escaped required notice: ' + package + '/' + str(path.relative_to(base)))
        name = package + '/' + path.relative_to(base).as_posix()
        payload[name] = path.read_bytes()
        return name

    def package(name, base, version, required, extras=()):
        if not base.resolve().is_relative_to(root):
            raise ValueError('Package is outside the pinned build sources: ' + name)
        paths = list(notice_files(base))
        for relative in [required, *extras]:
            path = base / relative
            if path not in paths:
                paths.append(path)
        components[name] = {'version': version, 'notices': sorted(add(name, base, path) for path in paths)}

    idf = root / 'build/esp-idf'
    sdk = root / 'build/sdk-components/arduino'
    libraries = root / 'build/arduino/libraries'
    # The library distributions include licenses for optional submodules as well.
    for name, version in lock['libraries'].items():
        base = libraries / name
        required = 'LICENCE.txt' if name == 'lvgl' else 'LICENSE.txt' if name == 'ArduinoJson' else 'license.txt'
        extras = ['scripts/generators/built_in_font/font_license/Montserrat/OFL.txt',
                  'scripts/generators/built_in_font/font_license/FontAwesome5/LICENSE.txt'] if name == 'lvgl' else []
        package('libraries/' + name, base, version, required, extras)
    package('arduino', sdk, lock['esp32_core'], 'LICENSE.md')
    payload['esp-idf/LICENSE'] = (idf / 'LICENSE').read_bytes()
    payload['VibeMon/LICENSE'] = (root / 'LICENSE').read_bytes()
    payload['VibeMon-App/LICENSE'] = (root / 'contracts/LICENSE-App.txt').read_bytes()
    payload['fonts/OFL-NanumGothic.txt'] = (root / 'assets/OFL-NanumGothic.txt').read_bytes()
    # Include the CA bundle's source form and its license alongside the converted firmware bundle.
    payload['certificates/cacrt_all.pem'] = (idf / 'components/mbedtls/esp_crt_bundle/cacrt_all.pem').read_bytes()
    payload['certificates/MPL-2.0.txt'] = (root / 'assets/MPL-2.0.txt').read_bytes()
    components['certificates'] = {'source': 'https://github.com/espressif/esp-idf/blob/' + lock['esp_idf']['commit'] + '/components/mbedtls/esp_crt_bundle/cacrt_all.pem',
                                  'licenseSource': 'https://curl.se/docs/caextract.html', 'notices': ['certificates/cacrt_all.pem', 'certificates/MPL-2.0.txt']}
    allowed = [idf, sdk, libraries, root / 'src', root / 'firmware', root / 'build/firmware-idf']
    for name, info in sorted(description['build_component_info'].items()):
        if not name or not info.get('dir'):
            continue
        base = Path(info['dir']).resolve()
        if not any(base.is_relative_to(path.resolve()) for path in allowed):
            raise ValueError('Component is outside the pinned build sources: ' + name)
        files = []
        if base.is_relative_to(idf):
            files.append('esp-idf/LICENSE')
            files.extend(add('components/' + name, base, path) for path in notice_files(base))
        headers = {}
        for source in info.get('sources', []):
            path = Path(source).resolve()
            if not path.is_file() or path.suffix.lower() not in {'.c', '.cpp', '.cc', '.s'}:
                continue
            if not any(path.is_relative_to(folder.resolve()) for folder in allowed):
                raise ValueError('Compiled source is outside the pinned build sources')
            with path.open('rb') as file:
                prefix = file.read(16384)
            header = LEADING_COMMENTS.match(prefix).group()
            if re.search(rb'copyright|license|licence|redistribution', header, re.IGNORECASE):
                digest = hashlib.sha256(header).hexdigest()
                output = 'attributions/' + digest + '.txt'
                payload[output] = header
                headers.setdefault(output, []).append(path.relative_to(root).as_posix())
        components['components/' + name] = {'notices': sorted(set(files)), 'sourceAttributions': headers}
    compiler = Path(description['c_compiler']).resolve().parent.parent
    if not compiler.is_relative_to((root / 'build/idf-tools').resolve()):
        raise ValueError('Compiler is outside the pinned toolchain')
    for name in ['gcc', 'newlib', 'picolibc']:
        base = compiler / 'share/licenses' / name
        if name == 'picolibc' and not base.exists():
            continue
        required = 'COPYING.RUNTIME' if name == 'gcc' else 'COPYING.NEWLIB' if name == 'newlib' else 'COPYING.picolibc'
        package('runtime/' + name, base, compiler.parent.name, required)
    components['VibeMon-App'] = {'revision': json.loads((root / 'app-contract.json').read_text())['revision'], 'notices': ['VibeMon-App/LICENSE']}
    result = write_notices(root / 'build/firmware/licenses', payload, components)
    verify_notices(root / 'build/firmware/licenses', result['sha256'])
    return result
