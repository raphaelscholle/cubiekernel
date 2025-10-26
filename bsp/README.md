# BSP integration

This kernel expects the Radxa-provided BSP repository `linux-a733` to live
under `bsp/linux-a733`. Run `./fetch-linux-a733.sh` from this directory to clone
or update it. The script uses `git` by default but honors the `GIT` and
`REPO_URL` environment variables if alternative binaries or mirrors are needed.

After fetching, `make O=build defconfig` will include the BSP Kconfig entries as
documented in the Radxa build guide.
