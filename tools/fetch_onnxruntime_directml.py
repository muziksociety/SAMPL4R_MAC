"""Fetch ONNX Runtime (DirectML build) + DirectML from NuGet into third_party/onnxruntime-directml.

SAMPLAR runs its ONNX models (htdemucs, Beat This!) through ONNX Runtime.
The DirectML build is the same ONNX Runtime 1.19.2 API with the CPU provider included, plus
the DirectML GPU provider that stem separation uses on Windows (any DX12 GPU). CMake prefers
this folder when it exists (SAMPLAR_ORT_DIRECTML=ON) and falls back to third_party/onnxruntime.

Layout written (gitignored, like third_party/onnxruntime):
    third_party/onnxruntime-directml/
        include/                  ONNX Runtime C/C++ headers (incl. dml_provider_factory.h)
        lib/onnxruntime.dll       DirectML build
        lib/onnxruntime.lib
        lib/DirectML.dll          Microsoft DirectML redistributable (loaded by onnxruntime.dll)
        LICENSE                   ONNX Runtime (MIT)
        ThirdPartyNotices.txt     ONNX Runtime third-party notices
        DirectML-LICENSE.txt      DirectML redistributable license
        DirectML-ThirdPartyNotices.txt

Usage: python tools/fetch_onnxruntime_directml.py   (from the repo root; needs internet once)
"""
import io, os, shutil, sys, urllib.request, zipfile

ORT_PKG, ORT_VER = 'Microsoft.ML.OnnxRuntime.DirectML', '1.19.2'   # same version as the CPU build
DML_PKG, DML_VER = 'Microsoft.AI.DirectML', '1.15.1'                # the version ORT 1.19.2 requires
DEST = os.path.join('third_party', 'onnxruntime-directml')


def nupkg(pkg, ver):
    url = f'https://api.nuget.org/v3-flatcontainer/{pkg.lower()}/{ver}/{pkg.lower()}.{ver}.nupkg'
    print('downloading', url)
    with urllib.request.urlopen(url, timeout=300) as r:
        return zipfile.ZipFile(io.BytesIO(r.read()))


def copy(z, member, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with z.open(member) as src, open(dest, 'wb') as dst:
        shutil.copyfileobj(src, dst)


def main():
    if not os.path.isdir('third_party'):
        sys.exit('run from the repository root')
    ort = nupkg(ORT_PKG, ORT_VER)
    for name in ort.namelist():
        if name.startswith('build/native/include/') and name.endswith('.h'):
            copy(ort, name, os.path.join(DEST, 'include', os.path.basename(name)))
    copy(ort, 'runtimes/win-x64/native/onnxruntime.dll', os.path.join(DEST, 'lib', 'onnxruntime.dll'))
    copy(ort, 'runtimes/win-x64/native/onnxruntime.lib', os.path.join(DEST, 'lib', 'onnxruntime.lib'))
    copy(ort, 'LICENSE', os.path.join(DEST, 'LICENSE'))
    copy(ort, 'ThirdPartyNotices.txt', os.path.join(DEST, 'ThirdPartyNotices.txt'))

    dml = nupkg(DML_PKG, DML_VER)
    copy(dml, 'bin/x64-win/DirectML.dll', os.path.join(DEST, 'lib', 'DirectML.dll'))
    copy(dml, 'LICENSE.txt', os.path.join(DEST, 'DirectML-LICENSE.txt'))
    copy(dml, 'ThirdPartyNotices.txt', os.path.join(DEST, 'DirectML-ThirdPartyNotices.txt'))

    with open(os.path.join(DEST, 'VERSION.txt'), 'w') as f:
        f.write(f'{ORT_PKG} {ORT_VER}\n{DML_PKG} {DML_VER}\n')
    print('done:', os.path.abspath(DEST))


if __name__ == '__main__':
    main()
