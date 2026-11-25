# fwenv-tool

Small standalone tool to read/write U-Boot environment variables from
Linux userspace. Works with plain env devices and raw MTD partitions,
including redundant (two-copy) setups.

Single static binary; behaviour selected by argv[0]:

| name          | action                          |
|---------------|---------------------------------|
| `fw_printenv` | print env (or selected vars)    |
| `fw_setenv`   | set / delete env variables      |
| `envtool`     | dump parsed configuration       |

## Build

    make CROSS=aarch64-linux-gnu-
    # or native:
    make CROSS=

Produces `fw_setenv` plus `fw_printenv` / `envtool` symlinks.

## Configuration

Searched in this order:

1. `$ENVTOOL_CONFIG`
2. `./fw_env.config`, `/etc/fw_env.config`, `/etc/fw_env.conf`
3. `./envtool.conf`, `/etc/envtool.conf`, `/tmp/envtool.conf`
4. autodetect (`/dev/env`, then common MTD partitions)

`fw_env.config` format (U-Boot compatible):

    # dev            off     size    [erase] [sectors]
    /dev/mtd0        0x0     0x4000  0x4000  1
    /dev/mtd1        0x0     0x4000  0x4000  1

`envtool.conf` format (legacy):

    device /dev/env
    offset 4
    size   0x10000
    fill   0x00

## Examples

    fw_printenv
    fw_printenv bootcmd
    fw_setenv bootargs 'console=ttyS0,115200'
    fw_setenv bootdelay          # delete variable
    envtool                      # show detected config
