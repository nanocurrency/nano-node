# Release pipeline

Two workflows turn a branch into published packages and images.

**Tag Build** (`tag_build.yml`) runs on develop every Wednesday and Saturday, or by hand on any branch.
It runs `next-tag.sh`, which picks the next build tag for the branch (`V29.0DB101` on develop,
`V29.1RC2` on `releases/v29`, or the release itself with the *release* box ticked), writes that
version into `CMakeLists.txt`, commits, tags and pushes the tag. Pre-release commits exist only
through their tag; a release commit is also pushed to its branch. It then starts Build & Deploy on
the new tag. Nothing happens when the latest tag already covers the branch head.

**Build & Deploy** (`build_deploy.yml`) runs on a build tag, so the commit it builds is the commit
it records. Run it by hand on a tag to rebuild that tag. For each network it

- builds the packages with `ci/build-packages.sh`, attests them and uploads them with `upload.sh`
  to `repo.nano.org/<beta|live>/binaries/`, each with a `.sha256` and the attestation bundle
  beside it,
- builds the node image with `docker-build.sh`, pushes it with `docker-push.sh` to
  `ghcr.io/nanocurrency/nano-node/<name>` and Docker Hub, and attests each push.

Images are named `nano` for a live release, `nano-nightly` for any other live build and `nano-beta`
for beta. Every push gets the tag; `latest` goes to ghcr.io always and to Docker Hub only for beta.

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
