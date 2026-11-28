# Repository layout

```
src/                the emulator, the qkz80 core, and the platform layer
                    under os/linux and os/windows; makefile, Makefile.win,
                    CMakeLists.txt and do_build.bat
tests/              run_tests.sh, its guests and the C++ harnesses; 8080/
                    holds the exercisers
util/               cpm_disk.py, its tests, and unreleased.sh
man/                the cpmemu.1 and cpm_disk.1 man pages
examples/           config file examples (the reference is docs/config_file.md)
packaging/windows/  MSIX packaging
docs/               the documents linked from README.md
.github/workflows/  ci.yml (the test suite), release.yml (deb, rpm, macOS)
                    and debian-upload.yml (the Debian source package)
LICENSE             the GNU GPL version 3
NOTICE              code from other projects, and the license of each
```

`cpm_disk`, the CP/M disk-image tool, ships wherever it can run: `make install`
installs it, and so do the `.deb`, the `.rpm` and the macOS archive. It is a
Python 3.7+ script, and the two Linux packages name `python3` as a
`Recommends:` rather than a `Depends:` so that installing a C++ emulator does
not pull Python onto a machine that has none. The Windows MSIX does not carry
it: an MSIX bundles no interpreter and cannot ask for one.
