# Contributing to tnerroT

First off, thanks for taking the time to contribute.

**tnerroT** is a personal project that aims to implement the BitTorrent protocol from scratch in C while keeping the codebase modular, readable, and faithful to the protocol specification. Whether you're fixing a bug, improving documentation, adding tests, or implementing a new feature, your contribution is appreciated.

## Getting Started

1. Fork the repository.
2. Create a feature branch.
3. Make your changes.
4. Ensure the project builds successfully and existing tests pass.
5. Open a pull request with a brief explanation of your changes.

## Coding Style

The goal is to keep the codebase clean, readable, and easy to extend.

A few guidelines:

* Keep functions focused on a single responsibility.
* Prefer descriptive names over abbreviations.
* Match the existing project structure and formatting.
* Avoid introducing unnecessary dependencies.
* Document public-facing functions using **Doxygen-style comments** whenever practical.
* If you're adding a feature, consider adding or updating tests alongside it.

## Commit Messages

I generally follow the format:

```text
Fix(src/tracker.c): handle malformed compact peer list

Feat(src/download.c): implement piece request queue

Refactor(src/bencoder.c): simplify integer parsing

Docs(README.md): update build instructions
```

Using a similar convention helps keep the project history organized, though it isn't a strict requirement.

## Testing

Good tests are just as valuable as good code.

If you're:

* fixing a bug, consider adding a regression test;
* implementing a new feature, include tests whenever practical;
* improving existing tests or increasing coverage, those contributions are always welcome.

## Areas Where Help Is Especially Appreciated

While I'll continue developing the project, there are several areas where community contributions can make a significant impact.

### Testing

Help expand the test suite by covering:

* malformed `.torrent` files
* invalid tracker responses
* protocol edge cases
* regression tests
* integration tests

### Performance & Scalability

As the project evolves into a complete download manager, performance will become increasingly important.

Contributions related to:

* stress testing
* benchmarking
* memory usage optimization
* high peer-count testing
* connection management
* download scheduling
* long-running stability

are especially valuable.

Real-world testing often reveals issues that small development environments cannot.

### Cross-Platform Support

The current implementation targets **Windows** and uses **WinSock** for networking.

A native Linux implementation (using POSIX sockets) is planned for the future. If you're interested in helping build or maintain a Linux-compatible networking layer while preserving a common codebase, your contribution would be greatly appreciated.

The long-term goal is to support both Windows and Linux with minimal platform-specific code.

### Documentation

Documentation improvements are always welcome, including:

* API documentation
* Doxygen comments
* architecture explanations
* protocol notes
* examples and tutorials

## Pull Requests

Please keep pull requests focused on a single logical change.

When opening a PR, include:

* what changed;
* why it was needed;
* how you tested it;
* any known limitations or follow-up work.

Constructive discussion is always welcome. Even if a proposal isn't merged, thoughtful issues and design discussions often help improve the project.

Thanks again for helping make **tnerroT** a better BitTorrent client.
