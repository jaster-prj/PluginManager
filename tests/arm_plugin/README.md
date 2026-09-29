# ARM Fixture

This is a constrained Cortex-M fixture, not a standalone Zephyr application.
Build it with the Zephyr SDK compiler prefix:

```text
cmake -S tests/arm_plugin -B build/arm-plugin \
  -DPM_ARM_TOOLCHAIN_PREFIX=/opt/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi
cmake --build build/arm-plugin
```

The generated ELF has no vector table or Zephyr runtime and uses the
PluginManager linker layout.
