# Changelog

All notable changes to `ideath` should be documented in this file.

The format is based on Keep a Changelog, adapted for the project's current stage.
`ideath` follows semantic versioning as described in [VERSIONING.md](VERSIONING.md).

## [Unreleased]

### Added

- CMake package export and install support for `find_package(ideath CONFIG REQUIRED)`
- Exported CMake target namespace `ideath::ideath`
- GitHub Actions CI for Linux and macOS build/test/install/package-consumer verification
- Contributor guide in [CONTRIBUTING.md](CONTRIBUTING.md)
- Versioning and release policy in [VERSIONING.md](VERSIONING.md)
- Assessment document in [ASSESSMENT.md](ASSESSMENT.md)

### Changed

- README usage examples now prefer `ideath::ideath`
- README now links to project governance and assessment docs

## Release Notes Template

Use this structure for future releases:

```md
## [X.Y.Z] - YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Removed
- ...
```
