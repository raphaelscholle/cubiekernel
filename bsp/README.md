# BSP integration

This kernel expects the Radxa-provided BSP repository `linux-a733` to live
under `bsp/linux-a733`. A placeholder directory is shipped so that Kconfig
parsing succeeds even before the BSP is available. Run `./fetch-linux-a733.sh`
from this directory to remove the placeholder and clone or update the real
repository. The script uses `git` by default but honors the `GIT` and `REPO_URL`
environment variables if alternative binaries or mirrors are needed.

After fetching, `make O=build defconfig` will include the BSP Kconfig entries as
documented in the Radxa build guide.
