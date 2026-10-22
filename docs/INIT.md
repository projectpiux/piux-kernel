# Piux Init

Piux provides a very basic init configuration file located at `/sbin/init`.

This file allows you to choose which system components should start automatically when Piux boots.

## Viewing the Init File

To view the current configuration, run:

```sh
cat /sbin/init
```

## Editing the Init File

To modify the configuration, run:

```sh
nano /sbin/init
```

## Configuration Syntax

You can add comments or disable startup entries by prefixing a line with `#`. Commented lines are ignored during startup.

For example:

```ini
# Default startup configuration

[STARTUP]
pde
# pwm
# I don't want to start pwm at boot, so I've commented it out.
```

In this example, `pde` will be started automatically, while `pwm` will be ignored because its entry is commented out.

You can use comments to document your configuration or temporarily disable components without deleting their entries.

**Note:** Only use component names supported by your current Piux version. The available startup options may change as Piux evolves.