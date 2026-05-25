# STM32 PWM Motor Control

这是一个基于课程项目整理和升级的 STM32 直流电机控制项目。项目从裸机轮询版本逐步整理到 FreeRTOS 多任务版本，用于演示 STM32 外设驱动、电机 PWM 控制、串口命令、OLED 状态显示和小型 RTOS 任务划分。

当前重点分支是 `final_freertos`：在原始完整功能基础上接入 FreeRTOS，保留电机控制、串口命令、OLED/LED 状态显示，并用任务调度替代裸机主循环轮询。

## 硬件平台

- MCU：STM32F103C6Tx
- 电机 PWM：
  - `TIM3_CH3 / PB0`：正转 PWM
  - `TIM3_CH4 / PB1`：反转 PWM
- 调速输入：`ADC1_IN1 / PA1` 电位器
- 按键：
  - `PB12`：运行按键
  - `PB13`：方向按键
- 串口：
  - `USART1`：115200，USB 串口调试/命令
  - `USART2`：9600，蓝牙串口命令/状态
- 显示：I2C OLED，使用 `I2C1`
- 指示灯：运行灯、报警灯、应用灯

## 软件工具链

- CMake
- Ninja
- Arm GNU Toolchain / `arm-none-eabi-gcc`
- STM32 HAL Driver
- FreeRTOS Kernel
- Python 3 + `pyserial`，用于 UART ISP 烧录脚本

macOS 下脚本默认兼容 xPack Arm GCC 常见安装路径；只要工具链在 `PATH` 里，也可以使用其它安装方式。

## 分支说明

| 分支 | 说明 |
| --- | --- |
| `main` | 原始完整功能裸机版 |
| `clean-version` | 整理后的干净裸机版 |
| `feature/freertos` | 最小 FreeRTOS 版，只保留 clean 功能并验证任务调度 |
| `final_freertos` | 完整功能 FreeRTOS 版，当前推荐展示和使用分支 |

## final_freertos 功能

- TIM3 双通道 PWM 控制电机正反转
- ADC 电位器调速
- PB12/PB13 按键输入，带 30ms 软件去抖
- 20ms 控制周期下的软启动/软减速
- 50ms 换向保护，避免正反转硬切换
- ADC 失败保护逻辑
- USART1 / USART2 串口命令输入
- `T` 命令设置状态上报周期
- `L0` / `L1` 控制应用灯
- `M0` 到 `M5` 切换控制模式
- OLED 显示电机状态、电压、目标占空比、上报周期
- LED 运行、报警、应用状态指示
- FreeRTOS 多任务调度

启动完成后，USART1 会输出一条简短日志：

```text
FreeRTOS motor control started
```

## FreeRTOS 任务划分

| 任务 | 周期/触发 | 职责 | 优先级 |
| --- | --- | --- | --- |
| `MotorTask` | 20ms，`vTaskDelayUntil()` | 调用电机控制状态机，处理 ADC、按键、PWM、软启动、换向保护 | 最高业务优先级 |
| `CommandTask` | UART 队列事件驱动 | 从队列读取 USART1/USART2 字节事件，调用命令解析 | 中等优先级 |
| `StatusTask` | 低优先级周期任务 | 初始化并刷新 OLED，处理 LED、ACK、串口状态上报 | 低优先级 |

UART 接收仍使用 `HAL_UART_Receive_IT()` 每次接收 1 字节。中断回调只判断来源、投递 FreeRTOS Queue、重新挂起接收；实际命令解析放在 `CommandTask`，避免在 ISR 中做复杂逻辑。

`StatusUi_Init()` 延后到 `StatusTask` 首次运行后执行，避免 OLED/I2C 初始化阻塞调度器启动。

## 串口命令

命令可从 USART1 或 USART2 输入。

| 命令 | 说明 |
| --- | --- |
| `T300` 到 `T2000` | 设置状态上报周期，单位 ms。超出范围会被限制到配置范围内 |
| `L0` | 关闭应用灯 |
| `L1` | 打开应用灯 |
| `M0` | 按键控制模式 |
| `M1` | 强制正转 |
| `M2` | 强制反转 |
| `M3` | 强制停止 |
| `M4` | 正转 100% 测试模式 |
| `M5` | 反转 100% 测试模式 |

状态上报示例：

```text
STATE:FORWARD,V:1.650,DUTY:50%,T:1000ms
```

部分命令会返回简短 ACK，例如：

```text
OK,T:1000ms
OK,M:1
```

## 构建方式

第一次使用前确认本机有：

- `cmake`
- `ninja`
- `arm-none-eabi-gcc`

构建：

```bash
./scripts/build.sh
```

默认会读取 [scripts/project.env](scripts/project.env)，使用 Release 配置并生成：

```text
build/416.elf
build/416.hex
build/416.bin
build/416.map
```

常用环境变量覆盖：

```bash
BUILD_TYPE=Debug CLEAN_BUILD=0 ./scripts/build.sh
```

## 烧录方式

项目提供 UART ISP 烧录脚本：

```bash
./scripts/flash-isp.sh
```

默认串口配置在 [scripts/project.env](scripts/project.env)：

```bash
PROJECT_NAME=416
ISP_PORT=/dev/cu.usbserial-1140
ISP_BAUD=115200
```

如果烧录串口不同，可以临时覆盖：

```bash
ISP_PORT=/dev/cu.usbserial-XXXX ./scripts/flash-isp.sh
```

只检查 HEX 文件解析，不真正烧录：

```bash
./scripts/flash-isp.sh --parse-only
```

## 目录结构

```text
Core/
  Inc/                    # 应用头文件、HAL 头文件、FreeRTOSConfig.h
  Src/                    # main、app、外设初始化、业务模块
Drivers/                  # STM32 HAL / CMSIS
Middlewares/
  Third_Party/
    FreeRTOS-Kernel/      # FreeRTOS Kernel 最小源码子集
cmake/                    # CMake toolchain 和 CubeMX 生成的 CMake 配置
scripts/
  build.sh                # CMake + Ninja 构建脚本
  flash-isp.sh            # UART ISP 烧录入口
  flash_isp.py            # Python ISP 下载工具
  project.env             # 构建/烧录默认配置
416.ioc                   # CubeMX 工程配置
CMakeLists.txt            # 顶层 CMake 工程
STM32F103XX_FLASH.ld      # 链接脚本
startup_stm32f103x6.s     # 启动文件
```

## 后续优化方向

- 给 FreeRTOS 任务栈增加高水位统计，进一步确认 10KB RAM 下的余量
- 将串口发送改为非阻塞或 DMA，减少状态上报对低优先级任务的阻塞
- 为 OLED/I2C 异常增加更明确的降级提示
- 增加关键模块的状态机说明图或时序图
- 将硬件接线表整理成独立文档或 README 表格
- 为不同分支补充更清晰的演示视频或截图

## 说明

本项目不是工业级电机控制方案，而是一个课程项目基础上的整理、重构和 FreeRTOS 升级示例。代码重点放在 STM32 外设使用、任务划分、状态机控制和嵌入式工程组织上。
