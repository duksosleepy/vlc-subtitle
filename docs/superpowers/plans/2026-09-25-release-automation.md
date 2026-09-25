# Implementation Plan: GitHub Actions Release Automation & Directory Organization

Relocate `.github/ci-windows.py` into `.github/workflows/`, implement a multi-platform automated GitHub Actions release workflow (`.github/workflows/release.yml`), and add local release helper scripts (`scripts/release.sh`, `scripts/changelog.sh`).

## User Requirements
- Move `ci-windows.py` into `.github/workflows/`.
- Create a new GitHub Action workflow that triggers on release branch creation/pushes (`v*.*.x`, `release/**`) and tag pushes (`v*`).
- Build artifacts across Windows (x64, x86), Linux (x64), and macOS (arm64).
- Package and publish a GitHub Release with assets and changelog.
- Provide `release.sh` and `changelog.sh` helper scripts.

---

## Proposed Changes

### 1. Relocate `.github/ci-windows.py`
- Move `.github/ci-windows.py` to `.github/workflows/ci-windows.py`.
- Update [.github/workflows/windows.yml](file:///home/duk/code/vlc-subtitle/.github/workflows/windows.yml) to call `py -3 .github/workflows/ci-windows.py`.

### 2. Create Release Workflow
#### [NEW] `.github/workflows/release.yml`
- Triggers on:
  - `push: branches: ['v*.*.x', 'release/**']`
  - `push: tags: ['v*']`
  - `workflow_dispatch`
- Jobs:
  - `build-windows`: builds Release x64 and x86, zips output into `vlc-subtitle-windows-x64.zip` and `vlc-subtitle-windows-x86.zip`.
  - `build-linux`: builds Release x64, archives into `vlc-subtitle-linux-x64.tar.gz`.
  - `build-macos`: builds Release arm64 on `macos-15`, archives into `vlc-subtitle-macos-arm64.tar.gz`.
  - `publish-release`: gathers archives, drafts or publishes the GitHub Release with attached assets.

### 3. Local Release Scripts
#### [NEW] `scripts/changelog.sh`
- Summarizes git commits since last tag in Markdown.
- Filters noise and linkifies commit/PR references.

#### [NEW] `scripts/release.sh`
- Calculates next semantic version `vM.N.0` (or uses argument).
- Creates and pushes tag and release branch `vM.N.x`.

---

## Verification Plan
1. Test moving `ci-windows.py` and verify `windows.yml` syntax.
2. Verify YAML syntax for `.github/workflows/release.yml`.
3. Test `scripts/changelog.sh` locally to ensure it outputs valid markdown.
4. Test `scripts/release.sh --help` parameter parsing.
