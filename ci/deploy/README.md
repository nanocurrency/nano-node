# Release pipeline

This directory holds the scripts behind the three GitHub Actions workflows that version, build and publish the node. The workflows live in `.github/workflows/`; the scripts live here so they can be read and dry-run outside Actions.

| Workflow | File | Runs on | Purpose |
|---|---|---|---|
| **Tag Build** | `tag_build.yml` | a branch | Creates the next build tag for the branch and starts Build & Deploy on it |
| **Prepare Release** | `prepare_release.yml` | `develop` | Opens the release branch of the current major and moves `develop` to the next major |
| **Build & Deploy** | `build_deploy.yml` | a build tag | Builds, attests and publishes the packages and Docker images of one tag |

"Run by hand" below means Actions → the workflow → *Run workflow*, with the branch or tag it should run on selected.

## Terms

- **Version**: the numbers `CPACK_PACKAGE_VERSION_MAJOR`, `MINOR` and `PRE_RELEASE` in `CMakeLists.txt`. The scripts read the major and minor from there and write the numbers a tag needs back into the file before tagging, so the tagged commit builds the version its tag names. The node binary reports these numbers, together with the tag, as its version.
- **Build tag**: a git tag `V<major>.<minor>...` created by Tag Build, see [Build tags](#build-tags). Build & Deploy runs only on build tags. The tag name reaches the build as `CI_TAG` and names every package and image.
- **Network**: `beta` or `live`. Every build tag is built for both networks from the same commit.
- **Default branch**: `develop`. Development builds of the next major are tagged there.
- **Release branch**: `releases/v<major>`, opened by Prepare Release. Release candidates and releases of that major are tagged there.

## Build tags

The tag Tag Build creates depends on the branch it runs on and on its *release* input:

| Kind | Branch | Tag | Example |
|---|---|---|---|
| Development build | `develop` | `V<major>.<minor>DB<n>` | `V29.0DB12` |
| Release candidate | `releases/v<major>` | `V<major>.<minor>RC<n>` | `V29.1RC2` |
| Release | `releases/v<major>`, *release* ticked | `V<major>.<minor>` | `V29.1` |
| Branch build | any other branch | `V<major>.<minor>-<branch>-<n>` | `V29.0-fix_vote_cache-3` |

`<major>` and `<minor>` come from `CMakeLists.txt` of the branch head. `releases/v<major>` is recognised as the release branch only when `<major>` matches the major in `CMakeLists.txt`. In a branch build, `<branch>` is the branch name with every character other than a letter or digit replaced by `_`. `<n>` is one more than the highest `<n>` among the existing tags of the same kind, starting at `1`.

Rules that apply to every run:

- **A tag is created only when HEAD is new.** Tag Build compares HEAD with the latest existing tag of the same kind. When that tag points at HEAD, or at a version commit made on top of HEAD, nothing is created and Build & Deploy is not started. The first tag of a kind (`V29.0DB1`, `V29.1RC1`, the first build of a branch) has no earlier tag to compare with and is always created.
- **The version is committed before tagging.** Tag Build writes the minor and, for a pre-release tag, the pre-release number `<n>` into `CMakeLists.txt` and commits the change as `Set version for <tag>`. When `CMakeLists.txt` already holds those numbers, HEAD is tagged as it is.
- **Pre-release commits are reachable only through their tag.** For development builds, release candidates and branch builds the version commit is not pushed to the branch, so the branch keeps the version it had.
- **Release commits are pushed to the branch.** For a release the version commit is pushed to `releases/v<major>` as well, so from then on the branch's `CMakeLists.txt` names the released version.
- **The minor advances after a release.** On `releases/v<major>` Tag Build starts from the minor in `CMakeLists.txt` and moves it up while the release tag `V<major>.<minor>` exists. Once `V29.0` exists, candidates are `V29.1RC<n>` and the next release is `V29.1`, without any edit to the branch.
- **The *release* input is accepted only on `releases/v<major>`.** On any other branch the run fails.
- **An existing tag is never recreated or moved.** When the tag to create already exists, the run fails.

Version commits and tags are authored as the workflow's actor, `<actor> <actor@users.noreply.github.com>`. Tags are annotated, with the message `Build tag <tag>`.

## What Build & Deploy publishes

Build & Deploy builds the tag's commit for both networks on three platforms and as a Docker image.

### Packages

Package files are named after the tag and the platform. The names are the same for both networks; the network is told apart by where a file is published.

| Platform | Runner | Files |
|---|---|---|
| Linux | `ubuntu-24.04` | `nano-node-<tag>-Linux.deb`, `nano-node-<tag>-Linux.tar.bz2` |
| macOS | `macos-26` | `nano-node-<tag>-Darwin.dmg` |
| Windows | `windows-2022` | `nano-node-<tag>-win64.exe`, `nano-node-<tag>-win64.zip` |

Beta packages are `RelWithDebInfo` builds, live packages `Release` builds. Both are portable builds and include the Qt wallet.

For each platform and network the packages are published to two places:

- **The run's workflow artifacts**, always, as one artifact `nano-node-<tag>-<platform>-<network>` with `<platform>` one of `Linux`, `Darwin`, `win64`.
- **The download bucket**, `s3://repo.nano.org/<network>/binaries/`, when the bucket credentials are configured (see [Repository configuration](#repository-configuration)). Every file is uploaded publicly readable, with a `<file>.sha256` beside it in `sha256sum -c` format.

Both places also receive `nano-node-<tag>-<platform>.sigstore.json`, the attestation bundle for that platform's packages (see [Attestations](#attestations)). The bundle gets no `.sha256`.

### Docker images

One image per network is built from `docker/node/Dockerfile` with the tag baked into the binaries. The image name depends on the network and on whether the tag is a release:

| Network | Tag | Image name |
|---|---|---|
| live | release, `V<major>.<minor>` | `nano` |
| live | any other | `nano-nightly` |
| beta | any | `nano-beta` |

The image is pushed to two registries:

| Registry | Repository | Tags pushed | When |
|---|---|---|---|
| GitHub Container Registry | `ghcr.io/nanocurrency/nano-node/<name>` | `<tag>` and `latest` | always |
| Docker Hub | `docker.io/nanocurrency/<name>` | `<tag>`, plus `latest` for beta only | when the Docker Hub credentials are configured |

So on `ghcr.io` the `latest` of every image name follows the newest build of that name. On Docker Hub only `nano-beta:latest` is moved by the pipeline; `nano:latest` and `nano-nightly:latest` are never touched, see [After the pipeline](#after-the-pipeline).

### Attestations

Every package set and every image push is attested with `actions/attest`: a statement, signed through GitHub's Sigstore instance, that the file digests or the image digest were produced by this repository's Build & Deploy workflow from the tag's commit.

- Package attestations are recorded in the repository's attestation store and published as a bundle file next to the packages.
- Image attestations are recorded in the attestation store and pushed to the registry next to the image.

[Verifying a download](#verifying-a-download) shows how to check a file or image against them.

## Workflows

### Tag Build

Creates the next build tag for the branch it runs on and starts Build & Deploy on it.

- **Triggers**: a schedule, Wednesday and Saturday at 00:00 UTC, which GitHub runs on the default branch; and by hand on any branch.
- **Input** `release` (boolean, default `false`): create the release tag `V<major>.<minor>` instead of a pre-release tag. Accepted only on `releases/v<major>`.
- **Steps**: check out the branch with its full history and all tags, run `ci/deploy/next-tag.sh`, and when it created a tag run `gh workflow run build_deploy.yml --ref <tag>`.
- **Permissions**: `contents: write` to push the version commit and the tag, `actions: write` to start Build & Deploy.
- **Concurrency**: one run per branch at a time; a second run on the same branch waits.

### Prepare Release

Opens the release branch for the major `develop` is at and moves `develop` on to the next major.

- **Trigger**: by hand only, on `develop`.
- **Preconditions**, checked by `ci/deploy/prepare-release.sh` before anything is pushed: the run is on the repository's default branch; `CMakeLists.txt` is at minor `0`; neither `releases/v<major>` nor `version-<major+1>.0` exists on `origin`.
- **Results**:
  - the branch `releases/v<major>`, created from HEAD with one commit, `Open releases/v<major> at version <major>.0`, that sets the pre-release number to `0`;
  - the branch `version-<major+1>.0`, created from HEAD with one commit, `Set develop to version <major+1>.0`, that sets the major to `<major+1>`;
  - a pull request from `version-<major+1>.0` to `develop` with the same title.
- **Permissions**: `contents: write` to push the two branches, `pull-requests: write` to open the pull request.

Merging the pull request is a manual step. GitHub does not run checks on a pull request opened with the workflow's token: push a commit to `version-<major+1>.0`, or close and reopen the pull request, to get them.

### Build & Deploy

Builds, attests and publishes the packages and images of one build tag.

- **Trigger**: by hand only, on a tag. Tag Build starts it on every tag it creates; run it yourself to rebuild a tag, see [Rebuilding or republishing a tag](#rebuilding-or-republishing-a-tag).
- **Job `Check Tag`** fails the run unless the ref is a tag starting with `V`. The tag name is passed to every job as `CI_TAG`.
- **Jobs `macOS`, `Linux`, `Windows`**, each once per network: prepare the runner with `ci/prepare/<os>/`, build the packages with `ci/build-packages.sh`, attest them, copy the attestation bundle into `packages/`, upload `packages/` as a workflow artifact, and run `ci/deploy/upload.sh packages/*` when `AWS_ACCESS_KEY_ID` is set.
- **Job `Docker`**, once per network: build the image with `ci/deploy/docker-build.sh`, push it to `ghcr.io` with `ci/deploy/docker-push.sh` and attest the push, then do the same for Docker Hub when `DOCKER_PASSWORD` is set.
- **Permissions**: `id-token: write` and `attestations: write` in every job for the attestations, `packages: write` in the Docker job for `ghcr.io`.

The four jobs, and the two network builds within each job, are independent: a failed build cancels no other build, so the run shows exactly which outputs of the tag were published.

## Scripts

All scripts are bash and stop with a `::error::` annotation when a precondition fails. Results are written to `$GITHUB_OUTPUT` when it is set and printed in any case.

| Script | Usage | Reads | Produces |
|---|---|---|---|
| `common.sh` | sourced by `next-tag.sh` and `prepare-release.sh` | | `cmake_version`, `set_cmake_version`, `git_as_actor`, `die` |
| `next-tag.sh` | `next-tag.sh [--dry-run]` | `RELEASE` (`true` or `false`), `GITHUB_REF_NAME` (else the checked-out branch), `GITHUB_ACTOR` | the tag; outputs `created` and `tag` |
| `prepare-release.sh` | `prepare-release.sh [--dry-run]` | `DEFAULT_BRANCH` (default `develop`), `GITHUB_REF_NAME`, `GITHUB_REPOSITORY`, `GITHUB_ACTOR`, `GH_TOKEN` | the two branches and the pull request; outputs `release_branch` and `pull_request` |
| `upload.sh` | `upload.sh <file>...` | `NETWORK`, `S3_BUCKET_NAME` (default `repo.nano.org`), `S3_BUILD_DIRECTORY` (optional prefix), AWS credentials from the environment | each file and its `.sha256` in the bucket |
| `docker-build.sh` | `docker-build.sh` | `NETWORK`, `CI_TAG`, `GITHUB_REPOSITORY` (for the image's source label) | the local image `nano-node:<tag>`; outputs `image` and `name` |
| `docker-push.sh` | `docker-push.sh <image> <repository> <tag>...` | a registry login made by the caller | the image under each tag; outputs `digest`, the manifest digest in that repository |

`ci/build-packages.sh`, one directory up, is the package build the platform jobs run. It reads `NETWORK` and `CI_TAG`, builds through `ci/build.sh` with the wallet enabled, runs the `cpack` that ships with the `cmake` in use, and leaves the packages in `packages/`.

## Release procedure

### Development builds

Nothing to do. The scheduled Tag Build runs on `develop` twice a week, tags its head `V<major>.0DB<n>` when it has moved since the last development tag, and Build & Deploy publishes the tag's packages and images.

### A major release, with 29.0 as the example

1. **Open the release branch.** Run Prepare Release on `develop` once it holds everything 29.0 should have. It creates `releases/v29` and opens the pull request that sets `develop` to 30.0. Merge that pull request. From then on `develop` builds are `V30.0DB<n>` and `releases/v29` builds are `V29.0RC<n>`.
2. **Build release candidates.** Run Tag Build on `releases/v29`. The first run creates `V29.0RC1` and publishes it. Fixes land on `releases/v29` through pull requests; after each, a further run creates `V29.0RC2`, `V29.0RC3` and so on. A run while the branch has not moved since the last candidate creates nothing.
3. **Build the release.** Run Tag Build on `releases/v29` with *release* ticked. It creates `V29.0`, pushes the version commit to `releases/v29`, and Build & Deploy publishes the release packages and the `nano` image.
4. **Publish the release**, see [After the pipeline](#after-the-pipeline).

### A point release, with 29.1 as the example

`releases/v29` stays the release branch for every 29.x and fixes land on it through pull requests. Because `V29.0` exists, Tag Build on `releases/v29` now creates `V29.1RC1`, `V29.1RC2` and so on, and with *release* ticked `V29.1`. Steps 2 to 4 above apply unchanged.

### Rebuilding or republishing a tag

Run Build & Deploy by hand on the tag. It builds the same commit and publishes under the same names: the files in the bucket and the image tags on both registries replace the earlier ones, the new run carries its own workflow artifacts, and a new attestation is recorded for everything it built. Tag Build cannot be used for this; it never recreates a tag.

### After the pipeline

The workflows stop at the bucket and the registries. These steps are manual:

- **GitHub release page.** Create the release for the tag and attach the packages from the bucket together with their `.sha256` files and the `nano-node-<tag>-<platform>.sigstore.json` bundles, so a download from the release page can be verified without the GitHub API.
- **Changelog.** The *Changelog* workflow (`changelog.yml`) generates the changelog for a release tag as a workflow artifact.
- **Live `latest` on Docker Hub.** The pipeline pushes `docker.io/nanocurrency/nano:<tag>` but never moves `nano:latest`. Move it by hand when the release should become the default image.

## Repository configuration

| Name | Kind | Used for | When unset |
|---|---|---|---|
| `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY` | secrets | the package upload to the bucket, region `us-east-2` | upload skipped; packages exist only as workflow artifacts |
| `S3_BUCKET_NAME` | variable | the bucket | `repo.nano.org` |
| `S3_BUILD_DIRECTORY` | variable | a prefix inside the bucket, before `<network>/binaries/` | no prefix |
| `DOCKER_PASSWORD` | secret | the Docker Hub login | Docker Hub push and attestation skipped |
| `DOCKER_USER` | variable | the Docker Hub user | `nanoreleaseteam` |
| `DOCKER_REGISTRY` | variable | the Docker Hub namespace | `nanocurrency` |

`ghcr.io` needs no configuration: the workflow's token pushes to `ghcr.io/<owner>/<repository>/<name>` of the repository the run belongs to.

On a fork without these secrets Build & Deploy therefore builds and attests everything, keeps the packages as workflow artifacts and pushes the images to the fork's own `ghcr.io` namespace. Prepare Release opens its pull request in the fork, never upstream.

## Running the scripts locally

`next-tag.sh` and `prepare-release.sh` run from a checkout of the branch in question. Fetch all tags first, since `next-tag.sh` counts the existing tags:

```bash
git fetch --tags
ci/deploy/next-tag.sh --dry-run
RELEASE=true ci/deploy/next-tag.sh --dry-run
ci/deploy/prepare-release.sh --dry-run
```

`--dry-run` prints the branch, the version in `CMakeLists.txt`, the tag or branches that would be created, or the check that fails, and stops before writing anything. Without `--dry-run` the scripts push to `origin` exactly as they do in the workflow, authored as the local user.

## Verifying a download

Attestations are checked with the GitHub CLI, which fetches them from the repository's attestation store:

```bash
gh attestation verify nano-node-V29.0-Linux.deb -R nanocurrency/nano-node --signer-workflow nanocurrency/nano-node/.github/workflows/build_deploy.yml
```

Images are checked the same way. `gh` reads the image's digest from the registry, so log in to the registry with `docker login` first:

```bash
gh attestation verify oci://ghcr.io/nanocurrency/nano-node/nano:V29.0 -R nanocurrency/nano-node
```

Without the GitHub CLI, the `.sigstore.json` bundle published next to the packages lets [cosign](https://github.com/sigstore/cosign) verify offline:

```bash
cosign verify-blob-attestation --type slsaprovenance1 --new-bundle-format --bundle nano-node-V29.0-Linux.sigstore.json --certificate-oidc-issuer https://token.actions.githubusercontent.com --certificate-identity-regexp '^https://github.com/nanocurrency/nano-node/\.github/workflows/build_deploy\.yml@' nano-node-V29.0-Linux.deb
```

The `.sha256` file beside each package checks integrity only, not origin:

```bash
sha256sum -c nano-node-V29.0-Linux.deb.sha256
```
