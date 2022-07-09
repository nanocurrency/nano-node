## Usage

### Build

`docker build . -f docker/sign/Dockerfile-signer -t <docker/image/tag>`

### Required Variables
* GPG_PRIVATE_KEY -> base64 encoded GPG private signing key
* GPG_SIGNER -> key id to sign with, must match GPG_PRIVATE_KEY
* S3_ACCESS_KEY_ID -> S3 Key with access to rpm/repo
* S3_SECRET_ACCES_KEY -> S3 Secret for S3_ACCESS_KEY_ID

### Sign RPM artifacts
`docker run --rm -it -e GPG_PRIVATE_KEY -e GPG_SIGNER -v </directory/containing/rpm/to/sign/>:/tmp <docker/image/tag> rpm-sign /tmp`

### Upload RPM artifacts
Upload artifacts to appropriate location

### Update Repo Metadata Files

`docker run --rm -it -e GPG_PRIVATE_KEY -e GPG_SIGNER -e S3_ACCESS_KEY_ID -e S3_SECRET_ACCESS_KEY <docker/image/tag> repo-update <s3://root/of/rpm/repo>`