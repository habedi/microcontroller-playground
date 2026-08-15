## Contribution Guidelines

Thank you for considering contributing to this project!
Contributions are always welcome and appreciated.

### How to Contribute

Please check the [issue tracker](https://github.com/habedi/microcontroller-playground/issues) to see if there is an issue
you would like to work on or if it has already been resolved.

#### Reporting Bugs

1. Open an issue on the [issue tracker](https://github.com/habedi/microcontroller-playground/issues).
2. Include information such as steps to reproduce the observed behavior and relevant logs or screenshots.

#### Suggesting Features

1. Open an issue on the [issue tracker](https://github.com/habedi/microcontroller-playground/issues).
2. Provide details about the feature, its purpose, and potential implementation ideas.

### Submitting Pull Requests

- Make sure the Git hooks pass (`make test-hooks`) before submitting a pull request.
- If the change touches an experiment, verify it on the real board and say so in the description.
- Write a clear description of the changes you made and the reasons behind them.

> [!IMPORTANT]
> It's assumed that by submitting a pull request, you agree to license your contributions under the project's license.

### Development Workflow

> [!IMPORTANT]
> If you're using an AI-assisted coding tool like Claude Code or Codex, make sure the AI follows the instructions in
> the root [AGENTS.md](AGENTS.md) file.

#### Prerequisites

Install GNU Make if it's not already installed on your system.

```shell
## For Debian-based systems like Debian, Ubuntu, etc.
sudo apt-get install make
```

- Use the `make submodules` command to fetch the git submodules under `external/`.
- Use the `make shell` command to enter the Nix dev shell, the primary development environment.
  On a Debian-based system without Nix, `make setup-deps` installs a partial set of dependencies with apt instead.
- Use the `make install` command to install the uv-managed Python tools.

#### Git Hooks

- Use the `make setup-hooks` command to install the Git hooks.
- Use the `make test-hooks` command to run the hooks on all files.

#### Building

- Use the `make nuttx-configure` and `make nuttx-build` commands to configure and build NuttX.
  See [AGENTS.md](AGENTS.md) for the boards, the flashing targets, and the validation expectations.

#### See Available Commands

- Run `make help` to see all available commands for managing different tasks.

### Code of Conduct

We adhere to the project's [Code of Conduct](CODE_OF_CONDUCT.md).
