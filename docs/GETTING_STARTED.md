# Getting Started (in-repo)

Operator install, USB flashing, and SSH login steps live at
https://deneb3d.dev/docs/getting-started/

This file keeps the bootstrap **host-package contract** used by CI and the
locked Pillow interpreter that `tools/build-get-started.sh` /
`tools/build-get-started.ps1` require. Do not duplicate a shortened apt recipe
in `README.md`.

Day-2 `.deneb` updates for operators: https://deneb3d.dev/docs/updating/

Operators can download a built `Deneb_get_started.img` from
https://github.com/jatmn/Deneb/releases/tag/get-started instead of running this
host build. This file remains the rebuild contract. GitHub Releases do not
publish `.deneb` stack packages. The Cura plugin package is a separate
`cura-plugin` release:
https://github.com/jatmn/Deneb/releases/tag/cura-plugin.

## Step 2: Build the get-started bootstrap package

This produces:

- `dist/Deneb_get_started.img`
- `dist/Deneb_get_started.img.sha256`

### Native Debian/Linux

Host packages needed for the bootstrap package (Python 3.10 or newer):

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends ca-certificates python3 python3-venv tar
python3 -m venv build/bootstrap-python
build/bootstrap-python/bin/python -m pip install --disable-pip-version-check \
  --only-binary=:all: --require-hashes -r tools/bootstrap-requirements.txt
```

Build:

```sh
DENEB_BOOTSTRAP_PYTHON="$PWD/build/bootstrap-python/bin/python" \
  bash tools/build-get-started.sh
```

### Windows with Debian WSL 2 or native PowerShell tooling

Python 3.10 or newer with Pillow is required on the Windows side for the
splash conversion:

```powershell
py -3 -m venv build/bootstrap-python
build/bootstrap-python/Scripts/python.exe -m pip install --disable-pip-version-check `
  --only-binary=:all: --require-hashes -r tools/bootstrap-requirements.txt
$env:DENEB_BOOTSTRAP_PYTHON = (Resolve-Path build/bootstrap-python/Scripts/python.exe).Path
powershell -ExecutionPolicy Bypass -File tools/build-get-started.ps1
```

Both lanes install the repository-locked Pillow wheels with verified hashes.
The builder must see that exact interpreter through `DENEB_BOOTSTRAP_PYTHON`.
System Python is not the locked bootstrap environment.
