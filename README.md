# Aura OS v2

Aura OS v2 is a clean-sheet ESP-IDF firmware project. The previous implementation is preserved on the `archive/aura-os-v1` branch.

## Current foundation

- Minimal ESP-IDF application
- Dedicated system task
- Clean build with no dependency on v1 components
- Safe failure behavior if the system task cannot start

## Build

Load ESP-IDF, then run:

```sh
idf.py set-target esp32s3
idf.py build
```

## Version 1

To inspect or restore the shelved implementation:

```sh
git switch archive/aura-os-v1
```

