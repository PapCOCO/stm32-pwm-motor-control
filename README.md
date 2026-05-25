# stm32-pwm-motor-control

[![Build STM32 Firmware](https://github.com/PapCOCO/stm32-pwm-motor-control/actions/workflows/build.yml/badge.svg)](https://github.com/PapCOCO/stm32-pwm-motor-control/actions/workflows/build.yml)

STM32F103C6T6 PWM DC motor control firmware based on STM32 HAL.

## Build

The primary build flow is CMake with the GNU Arm Embedded toolchain. GitHub Actions builds the firmware on `ubuntu-latest` and uploads the generated firmware files as a downloadable artifact named `stm32-firmware`.

The CI build uses Release mode because the STM32F103C6T6 has 32 KB of Flash.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

Expected build outputs:

```text
build/416.elf
build/416.hex
build/416.bin
build/416.map
```

## Project Structure

```text
Core/
  Inc/                         Application and peripheral headers
  Src/
    main.c                     Program entry point and main loop
    tim.c                      TIM2 and TIM3 initialization
    motor_control.c            Motor state machine and PWM duty control
    command.c                  UART command parser
    status_ui.c                LED, OLED, and serial status reporting
    adc.c/gpio.c/i2c.c/usart.c CubeMX peripheral initialization

Drivers/
  CMSIS/                       CMSIS core and STM32F1 device headers
  STM32F1xx_HAL_Driver/        STM32 HAL driver source and headers

MDK-ARM/                       Keil MDK project files and MDK startup file
cmake/                         CMake toolchain files and CubeMX CMake glue
scripts/                       Utility scripts

CMakeLists.txt                 Main CMake project
416.ioc                        STM32CubeMX project configuration
STM32F103XX_FLASH.ld           GCC linker script for 32 KB Flash / 10 KB RAM
startup_stm32f103x6.s          GCC startup file used by the CMake build
```

## Firmware Overview

- MCU: STM32F103C6T6
- Framework: STM32 HAL
- PWM output: TIM3 CH3 on PB0 and TIM3 CH4 on PB1
- Motor control module: `Core/Src/motor_control.c`
- Main entry point: `Core/Src/main.c`
- Timer setup: `Core/Src/tim.c`
