# Signing and notarizing the macOS release

The macOS release is signed ad hoc and is not notarized, so a browser download
is refused by Gatekeeper and `README.md` tells the user to strip the quarantine
attribute by hand. This file is what it would take to stop doing that.

**None of this has run.** The steps are in `.github/workflows/release.yml`, in
the `build-macos` job, and each is gated on `env.HAVE_SIGNING` or
`env.HAVE_NOTARY`, both of which are false because the secrets are not set.
Until then the job builds and packages exactly as it did, and the workflow is
green with the signing steps skipped.

Nothing else in the workflow has to change. What is left is a setup task and a
first real run: create the Developer ID Application certificate, export it,
mint the notary key, set the five secrets, and then cut a release and read the
log — because none of the signing path has ever executed, and a keychain
import, `security set-key-partition-list`, the per-architecture `CDHash`
comparison after `cpack` and `notarytool --wait` are four things that have
never been observed working here.

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
	base64 `.p12`.

	**It has to be created before it can be exported, and it is not the
	certificate the App Store already uses.** A store or TestFlight build
	is signed with an App Store distribution certificate; the store signs
	its own copy and runs its own equivalent checks, and none of that
	reaches a tarball shipped from GitHub. Apple is explicit that using the
	wrong one fails late rather than early: "You can only notarize apps
	that you sign with a Developer ID certificate. If you use any other
	certificate — like a Mac App Distribution certificate, or a self-signed
	certificate — notarization fails." The ad-hoc signature this project
	ships today is on the same list.

	So: developer.apple.com → Certificates, Identifiers & Profiles →
	Certificates → + → Developer ID Application, or Xcode → Settings →
	Accounts → Manage Certificates → + → Developer ID Application. Then
	Keychain Access → My Certificates → right-click → Export as `.p12`
	with a password, and `base64 -i cert.p12 | pbcopy`. Export it on the
	machine that generated the request: a certificate downloaded onto a
	different Mac has no private key attached and cannot be exported as a
	usable `.p12`.

	Three things worth knowing before starting, each of which is otherwise
	discovered at the worst moment:

	- **Only the Account Holder can create one.** Not Admin. Apple's role
	  table checks "Create Developer ID certificates" for Account Holder
	  alone, while ordinary distribution certificates are also open to
	  Admin and App Manager. If the enrolment is an individual one this is
	  moot — an individual is their own Account Holder. On an organization
	  account it may be someone else. (There is an Admin-accessible
	  "cloud-managed" Developer ID certificate, but Xcode holds its key and
	  there is no `.p12` to export, so it is no use to CI.)
	- **Five per team, and no self-service way past that.** Developer ID is
	  exempt from the one-certificate-per-team rule that binds the others,
	  with a budget of five Application and five Installer certificates. An
	  account that has shipped for a while may have spent them; past the
	  limit only Developer Programs Support can help.
	- **Nothing here costs money.** The $99 membership that makes App Store
	  and TestFlight possible is the same one that covers this, and Apple
	  lists distributing outside the Mac App Store with a Developer ID
	  certificate as a benefit of it.

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

	**It must be a Team key.** Apple: "Individual keys aren't able to use
	Provisioning endpoints, access Sales and Finance, or notaryTool." So a
	key made under the Individual API Key section of a user profile is
	rejected by the very tool it is here for. Generate it from the Team
	Keys tab instead. The word collides confusingly with enrolment type:
	somebody *enrolled as an individual* is their own team's Admin and can
	make Team keys perfectly well — it is the key's type that matters, not
	the membership's.

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
one above. Same membership, same Account Holder restriction, and its own budget
of five — so it is another certificate to create, not another thing to buy.

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
