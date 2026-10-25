# Contributing

Thank you for helping with Hawser. Issues and pull requests are welcome (merge
requests, on GitLab).

## Before you start

Open an issue first for anything bigger than a small fix, so the change can
be talked through before the work is done. Bug reports are most useful with
the output of `hawser status --json` from the machines involved, the kernel
version, and what you expected to happen.

## Making a change

1. Branch from `dev`. `dev` takes merge requests; `prod` moves only when `dev`
   is merged into it, and that publishes a release.
2. Keep the code readable. One idea per function, named for what it does, and
   a comment only where the code cannot say it. The code is C++20 with no
   dependencies beyond the kernel and the WireGuard library in `third_party`.
3. Add or change a test for what you changed. The unit tests live in `tests/`;
   `scripts/network-test.sh` runs a real network of four containers and checks
   what the README promises.
4. Run the tests before you open the merge request:

   ```sh
   cmake -S . -B build && cmake --build build -j && ./build/hawser_tests
   scripts/network-test.sh
   ```

5. If a change alters what a user sees or does, change the README in the same
   merge request.

## Commits

Write the subject as `type(scope): what changed`, in the imperative, for
example `fix(path): keep the relayed path while the direct one is tried`.
Types in use: `feat`, `fix`, `docs`, `test`, `refactor`, `build`, `ci`.

## License

Hawser is licensed under the [Apache License 2.0](LICENSE). By contributing,
you agree that your contribution is licensed under the same terms. Do not edit
the files in `third_party/wireguard`; they are taken unchanged from
wireguard-tools and are under their own license.
