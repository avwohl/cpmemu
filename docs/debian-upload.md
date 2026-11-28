# The Debian upload

cpmemu goes into Debian through a sponsor. The ITP is
[bug #1155310](https://bugs.debian.org/1155310). The packaging is on the
DEP-14 branch `debian/latest`, which has each upstream release tag merged and
adds the `debian/` directory. A signed source package goes to
[mentors.debian.net](https://mentors.debian.net). A sponsor gets the package
from there, examines it, and uploads it to Debian. Ubuntu copies new packages
from Debian unstable automatically.

The workflow `.github/workflows/debian-upload.yml` ("Debian source package")
makes the signed source package. The workflow starts only by hand, and it
uploads to mentors.debian.net only when you set the `upload` input.

## What the workflow does

The workflow has two jobs. Each job runs in its own `debian:sid` container on
its own runner.

The `build` job builds the tip of `debian/latest`:

1. It downloads the orig tarball of the upstream version in
   `debian/changelog` from GitHub. This file is the file that `uscan` finds
   through `debian/watch`. The job compares its files with `git archive` of
   the tag.
2. It makes the source package, then builds the binary package. The binary
   build runs the test suite.
3. It runs lintian and the autopkgtest. An error or a warning from lintian
   stops the run.
4. It keeps the unsigned files as the run artifact `cpmemu-debian-build`.

The `sign` job starts after the `build` job:

1. It installs only `devscripts`, `dput` and `gnupg`, and it gets the files
   from the `cpmemu-debian-build` artifact.
2. It signs the source package with the key in the secret.
3. It verifies the signatures with `dscverify` and with `dput -s`, which
   sends nothing.
4. It keeps the signed files as the run artifact `cpmemu-debian-source`.
5. If `upload` is set, it uploads the source package to mentors.debian.net
   with `dput` over HTTPS.

Only the sign step of the `sign` job gets the secrets. The `build` job runs
code from the package as root, so the `build` job gets no secret. The `sign`
job runs no code from the package. The sign step deletes its keyring before
it ends.

## The secrets

`XDEB_GPG_PRIVATE_KEY`: the secret key as an ASCII-armored export. One user
ID of the key must have the address in the trailer line of
`debian/changelog`, `xdeb@awohl.com`.

`XDEB_GPG_PASSPHRASE`: the passphrase of the key. Set this secret only if the
key has a passphrase.

To set the secrets, do these steps on the machine that has the key. Do the
second `gh secret set` only if the key has a passphrase. That command asks for
the passphrase.

```
gpg --armor --export-secret-keys xdeb@awohl.com > xdeb.secret.asc
gh secret set XDEB_GPG_PRIVATE_KEY -R avwohl/cpmemu < xdeb.secret.asc
gh secret set XDEB_GPG_PASSPHRASE -R avwohl/cpmemu
```

### Hold the secrets in the environment

A secret of the repository is available to each workflow on each branch. The
`sign` job uses the environment `debian-upload`. A secret of that environment
is available only to jobs that use the environment. A branch rule can limit
the environment to `main`. To move the secrets into the environment, do these
steps. Do the two passphrase commands only if the key has a passphrase.

```
gh api -X PUT repos/avwohl/cpmemu/environments/debian-upload \
  -F 'deployment_branch_policy[protected_branches]=false' \
  -F 'deployment_branch_policy[custom_branch_policies]=true'
gh api -X POST repos/avwohl/cpmemu/environments/debian-upload/deployment-branch-policies \
  -f name=main
gh secret set XDEB_GPG_PRIVATE_KEY -R avwohl/cpmemu --env debian-upload < xdeb.secret.asc
gh secret delete XDEB_GPG_PRIVATE_KEY -R avwohl/cpmemu
gh secret set XDEB_GPG_PASSPHRASE -R avwohl/cpmemu --env debian-upload
gh secret delete XDEB_GPG_PASSPHRASE -R avwohl/cpmemu
```

The branch rule applies to the branch that starts the run. The rule does not
apply to `debian/latest`, which the `build` job builds, but the `build` job
gets no secret.

## Before the first upload

1. Make an account on mentors.debian.net.
2. Add the public key to the account. This command writes the public key:
   `gpg --armor --export xdeb@awohl.com`
3. Make sure that the top entry of `debian/changelog` on `debian/latest`
   names `unstable`. If the entry names `UNRELEASED`, the workflow stops an
   upload run before the build.

## Run the workflow

1. Start a run with no upload:
   `gh workflow run debian-upload.yml -R avwohl/cpmemu -f upload=false`
2. Wait for the run: `gh run watch -R avwohl/cpmemu`
3. Download the artifact and examine the files:
   `gh run download RUN-ID -R avwohl/cpmemu -n cpmemu-debian-source`
4. Start a run with the upload:
   `gh workflow run debian-upload.yml -R avwohl/cpmemu -f upload=true`

mentors.debian.net sends an email when it accepts the upload. The page of the
package on mentors.debian.net has an RFS template. Send the RFS as a bug
against the `sponsorship-requests` pseudo-package.

## A new upstream release

1. Make the release on `main` as usual, with the tag `vX.Y.Z`.
2. On `debian/latest`, merge the tag: `git merge vX.Y.Z`
3. Add a `debian/changelog` entry for `X.Y.Z-1` that names `unstable`.
4. Push `debian/latest`, then run the workflow.
