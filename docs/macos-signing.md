# Signing and notarizing the macOS release

The macOS release is signed ad hoc and is not notarized, so a browser download
is refused by Gatekeeper and `README.md` tells the user to strip the quarantine
attribute by hand. This file is what it would take to stop doing that.

**None of this has run.** The steps are in `.github/workflows/release.yml`, in
the `build-macos` job, and each is gated on `env.HAVE_SIGNING` or
`env.HAVE_NOTARY`, both of which are false because the secrets are not set.
Adding the five secrets below is the whole of the remaining work; nothing else
in the workflow has to change. Until then the job builds and packages exactly
as it did, and the workflow is green with the signing steps skipped.

## What it is worth, and what it is not

A notarization ticket **cannot be stapled to a bare Mach-O executable**.
`stapler` takes UDIF disk images, flat installer packages and code-signed
bundles; Apple's position is that a ticket is created for a standalone binary
but cannot be attached to one. This release is one bare binary inside a
`.tar.gz`, so the best available outcome is *notarized and unstapled*, and
Gatekeeper looks the ticket up over the network:

	the user	today	signed, notarized, unstapled
	downloads with curl, runs it	works; curl sets no quarantine attribute	works
	browser download, online	refused silently: sits in state SN	works
	browser download, offline	refused silently	still refused

So notarizing removes the `xattr` step for everyone except a user who is both
downloading through a browser and offline. Covering that last case too means
shipping a `.pkg` or a `.dmg`, which can be stapled — a different artifact, a
different install instruction in `README.md`, and a decision about what the
download is, not a change to make quietly inside a release job.

## The secrets

Create these five under Settings → Secrets and variables → Actions. There are
two independent gates: the certificate pair turns on signing, and the three
notary values turn on notarization. A certificate with no notary credentials
signs and does not notarize, rather than failing, so they can be added in two
sittings.

`secrets` cannot be read from a step's `if:` — the contexts available there are
`github`, `needs`, `strategy`, `matrix`, `job`, `runner`, `env`, `vars`,
`steps` and `inputs`, and a job-level `if:` gets fewer still. A job-level
`env:` *can* read secrets. That is why the gate is a job-level `env:` block
that the steps then test, and not a `secrets` expression in each step.

### Signing

`MACOS_CERTIFICATE_P12`
	The Developer ID Application certificate and its private key, as a
	base64 `.p12`. Export it from Keychain Access (right-click the
	certificate → Export), then `base64 -i cert.p12 | pbcopy`. Getting the
	certificate in the first place needs a paid Apple Developer Program
	membership; that is the part of this that costs money and cannot be
	done from here.

`MACOS_CERTIFICATE_PASSWORD`
	The password set when exporting the `.p12`.

`MACOS_SIGN_IDENTITY`
	The identity string `codesign` matches, in full — for example
	`Developer ID Application: Some Name (TEAMID1234)`. `security
	find-identity -v -p codesigning` lists what a keychain holds.

### Notarizing

`MACOS_NOTARY_KEY`
	An App Store Connect API key, as a base64 `.p8`. Create it at App Store
	Connect → Users and Access → Integrations → App Store Connect API, with
	the Developer role. The `.p8` downloads once and cannot be downloaded
	again. `base64 -i AuthKey_XXXXXXXXXX.p8 | pbcopy`.

`MACOS_NOTARY_KEY_ID`
	The key ID beside it, the `XXXXXXXXXX` in the filename.

`MACOS_NOTARY_ISSUER_ID`
	The issuer UUID, shown once per team at the top of that same page.

An Apple ID with an app-specific password works with `notarytool` too, in place
of the last three. The API key is used here because it does not expire when the
account's password changes and does not carry a person's Apple ID into CI.

Bring them up in three passes, so a failure says which half is wrong: run the
workflow with no secrets and check all four gated steps report *skipped* and
the tarball is the shape it is today; add the certificate pair alone and check
signing passes while notarizing skips; then add the notary three and check the
job log shows `"status":"Accepted"`.

The `.pkg` route, if the offline and double-click cases ever matter, needs one
more secret: installer packages are signed with a Developer ID **Installer**
certificate, which is a different `.p12` from the Developer ID **Application**
one above. Both come with the same membership, so it is another export, not
another purchase.

## After the first signed release

Check the release from a machine that did not build it:

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
codesign -dv --verbose=4 cpmemu-*-Darwin-arm64-x86_64/bin/cpmemu
spctl --assess --type execute --verbose=4 cpmemu-*-Darwin-arm64-x86_64/bin/cpmemu
```

`spctl` should say `accepted` and `source=Notarized Developer ID`. If it says
`rejected`, the ticket is not being found and the `xattr` line in `README.md`
still has to stay. Then update `README.md`: the `xattr` step becomes something
only an offline browser download needs, rather than something everyone needs.
