# Release pipeline

## How a release happens

Development builds come out of develop by themselves: **Tag Build** runs there every Wednesday and
Saturday, tags the branch head `V29.0DB<n>` and starts **Build & Deploy**, which publishes the beta
and live packages and images for that tag. When develop has not moved since the last tag, nothing
is built.

To release 29.0:

1. Run **Prepare Release** on develop once it holds everything the release should. It opens
   `releases/v29` with the pre-release number set to 0 and a pull request that sets develop to
   30.0. Merge that pull request; develop's builds are now `V30.0DB<n>`. A pull request opened by
   a workflow gets no checks of its own, so push to its branch or close and reopen it if you want
   them.
2. Run **Tag Build** on `releases/v29` for a release candidate, `V29.0RC1`. Fixes go onto the
   release branch through pull requests as usual; every further Tag Build run on it gives the next
   candidate, `V29.0RC2` and so on, and does nothing when the branch has not moved.
3. Run **Tag Build** on `releases/v29` with the *release* box ticked for the release itself,
   `V29.0`. This time the version commit is also pushed to the release branch, so the branch's
   CMakeLists.txt names the released version.

For a point release the branch simply carries on: fixes land on `releases/v29`, Tag Build gives
`V29.1RC1` since `V29.0` exists, and the release box gives `V29.1`.

To rebuild a tag whose Build & Deploy failed, or to republish it, run **Build & Deploy** by hand on
that tag. It publishes under the same names, replacing what is in the bucket and on the registries,
and records a new attestation for what it built. Tag Build never recreates a tag.

The workflows stop at the bucket and the registries. The GitHub release page with its assets and
the live `latest` tag on Docker Hub are made by hand; when attaching the packages to the release,
attach their `.sigstore.json` bundles from the bucket as well, so the release can be verified
without the GitHub API.

## The workflows

**Tag Build** (`tag_build.yml`) runs `next-tag.sh`, which picks the next build tag for the branch:
`V29.0DB<n>` on develop, `V29.<minor>RC<n>` on the release branch of the current major, the plain
release `V29.<minor>` with the *release* box, and `V29.0-<branch>-<n>` for any other branch. It
writes
that version into `CMakeLists.txt`, commits, tags and pushes the tag, then starts Build & Deploy on
it with `gh workflow run`. Pre-release commits exist only through their tag; a release commit is
also pushed to its branch. The minor moves on by itself once the release branch has a released
tag at the current minor. Nothing happens when the latest tag of the kind already covers HEAD.

**Prepare Release** (`prepare_release.yml`) runs `prepare-release.sh`, which refuses to run on
anything but the default branch or when the branch is not at `<major>.0`. It pushes
`releases/v<major>` from HEAD with the pre-release number set to 0, pushes a `version-<major+1>.0`
branch with the major bumped, and opens the pull request for it.

**Build & Deploy** (`build_deploy.yml`) runs on a build tag, so the commit it builds is the commit
it records; its first job refuses anything but a tag. For each network it

- builds the packages with `ci/build-packages.sh`, attests them, keeps them with the run as a
  workflow artifact named `nano-node-<tag>-<platform>-<network>`, and uploads them with
  `upload.sh` to `repo.nano.org/<beta|live>/binaries/`, each with a `.sha256` and the attestation
  bundle beside it;
- builds the node image with `docker-build.sh`, pushes it with `docker-push.sh` to
  `ghcr.io/nanocurrency/nano-node/<name>` and Docker Hub, and attests each push.

Images are named `nano` for a live release, `nano-nightly` for any other live build and `nano-beta`
for beta. Every push gets the tag; `latest` goes to ghcr.io always and to Docker Hub only for beta.
On a fork, where the bucket and Docker Hub credentials are missing, those uploads are skipped and
the packages are only kept as workflow artifacts.

Both scripts that edit the version take `--dry-run`, which prints what they would do and stops.

## Verifying a download

Attestations are signed statements, made by GitHub Actions, that a file or image digest was built
by this repository's workflow from a given commit. The GitHub CLI checks a package against them:

```bash
gh attestation verify nano-node-V29.0-Linux.deb -R nanocurrency/nano-node --signer-workflow nanocurrency/nano-node/.github/workflows/build_deploy.yml
```

Images are checked the same way after a `docker login` to the registry:

```bash
gh attestation verify oci://ghcr.io/nanocurrency/nano-node/nano:V29.0 -R nanocurrency/nano-node
```

Without the GitHub CLI, the `.sigstore.json` bundle published next to the packages lets
[cosign](https://github.com/sigstore/cosign) verify offline:

```bash
cosign verify-blob-attestation --new-bundle-format --bundle nano-node-V29.0-Linux.sigstore.json --certificate-oidc-issuer https://token.actions.githubusercontent.com --certificate-identity-regexp '^https://github.com/nanocurrency/nano-node/\.github/workflows/build_deploy\.yml@' nano-node-V29.0-Linux.deb
```
