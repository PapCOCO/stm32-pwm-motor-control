# STM32 PWM Motor Control
基于 STM32F103C6Tx 的直流电机 PWM 控制项目。  
本分支为原始完整功能版，采用裸机主循环调度，不使用 FreeRTOS。
项目实现了电机正反转 PWM 控制、电位器调速、按键控制、串口命令控制、OLED 状态显示、LED 指示和 UART ISP 烧录脚本。
---
## 一、项目概览
### 硬件平台
- MCU：STM32F103C6Tx
- 电机控制：TIM3 双通道 PWM
- 调速输入：ADC1_IN1 / PA1 电位器
- 按键输入：
  - PB12：运行控制
  - PB13：方向控制
- 显示：OLED，I2C1
- 串口：
  - USART1：USB 串口调试 / 命令输入
  - USART2：蓝牙串口 / 第二路命令输入
- 烧录方式：UART ISP 串口烧录
---
## 二、主要功能
### 1. 电机控制
- 使用 `TIM3_CH3 / PB0` 输出正转 PWM
- 使用 `TIM3_CH4 / PB1` 输出反转 PWM
- 同一时间只允许一个方向输出 PWM，避免正反转同时导通
- 通过 ADC 读取电位器电压，并映射为目标占空比
- 支持软启动和软减速，避免 PWM 突变
- 支持换向保护：方向切换时先降速到 0，再等待后切换方向
- ADC 读取异常时自动保护停机
### 2. 按键控制
- PB12：运行 / 停止控制
- PB13：方向切换
- 软件去抖时间：30ms
### 3. 串口命令控制
USART1 和 USART2 均支持命令输入。
支持命令：
| 命令 | 功能 |
|---|---|
| `T数字` | 设置状态上报周期，单位 ms |
| `L0` | 关闭应用 LED |
| `L1` | 打开应用 LED |
| `M0` | 按键控制模式 |
| `M1` | 强制正转 |
| `M2` | 强制反转 |
| `M3` | 强制停止 |
| `M4` | 测试正转 100% |
| `M5` | 测试反转 100% |
示例：
```text
T1000
M1
M3
L1

4. OLED / LED 状态显示

OLED 显示内容包括：

* 当前方向
* 当前电压 / ADC 状态
* 目标占空比
* 状态上报周期
* 固定标签信息

LED 用于显示：

* 运行状态
* 报警状态
* 应用灯状态

5. 构建与烧录脚本

项目提供 macOS 下的构建和 UART ISP 烧录脚本：

./scripts/build.sh
./scripts/flash-isp.sh

⸻

三、软件结构

核心业务文件：

Core/Src/
├── main.c              # 程序入口、外设初始化、裸机主循环调度
├── motor_control.c     # 电机控制状态机、ADC、PWM、按键、换向保护
├── command.c           # 串口命令解析、控制模式、ACK 回包
├── app_uart.c          # UART 发送封装
├── status_ui.c         # OLED / LED / 串口状态上报
├── oled.c              # OLED 底层驱动

对应头文件：

Core/Inc/
├── app_config.h
├── motor_control.h
├── command.h
├── app_uart.h
├── status_ui.h
├── oled.h
├── oledfont.h

CubeMX / HAL 生成文件：

Core/Src/
├── adc.c
├── gpio.c
├── i2c.c
├── tim.c
├── usart.c
├── stm32f1xx_it.c
├── stm32f1xx_hal_msp.c
├── system_stm32f1xx.c

这些文件主要负责外设初始化和中断入口，一般不需要频繁修改。

⸻

四、主循环调度方式

本分支采用裸机调度方式。

main.c 中完成 HAL、时钟和外设初始化后，启动业务模块：

App_StartPeripherals();

主循环中按时间片执行：

while (1)
{
    MotorControl_Task(...);
    Command_ProcessAck();
    StatusUi_Task(...);
}

主要调度逻辑：

* MotorControl_Task()：按固定周期执行电机控制
* Command_ProcessAck()：处理串口命令回包
* StatusUi_Task()：处理 OLED、LED 和周期状态上报
* HAL_UART_RxCpltCallback()：串口中断接收 1 字节并交给命令解析模块

⸻

五、构建环境

工具链

推荐环境：

* macOS
* CMake
* Ninja
* xPack GNU Arm Embedded GCC
* Python 3
* pyserial

检查工具：

cmake --version
ninja --version
arm-none-eabi-gcc --version
python3 --version

安装 pyserial：

python3 -m pip install pyserial

⸻

六、构建方法

在项目根目录执行：

./scripts/build.sh

构建成功后会生成：

build/416.elf
build/416.hex
build/416.bin
build/416.map

其中：

文件	用途
416.elf	调试文件，包含符号信息
416.hex	常用烧录文件
416.bin	二进制固件
416.map	内存和符号分布信息

⸻

七、烧录方法

本项目使用 UART ISP 串口烧录。

默认烧录命令：

./scripts/flash-isp.sh

如果串口不是默认的 /dev/cu.usbserial-1140，可以临时指定：

ISP_PORT=/dev/cu.usbserial-XXXX ./scripts/flash-isp.sh

只解析 HEX，不实际烧录：

./scripts/flash-isp.sh --parse-only

⸻

八、脚本配置

脚本配置集中在：

scripts/project.env

常用配置项：

PROJECT_NAME=416
BUILD_DIR=build
BUILD_TYPE=Release
CMAKE_GENERATOR=Ninja
CMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
ISP_HEX=build/416.hex
ISP_PORT=/dev/cu.usbserial-1140
ISP_BAUD=115200

⸻

九、分支说明

本仓库包含多个版本分支：

分支	说明
main	原始完整功能裸机版
clean-version	简化后的裸机版本，保留核心电机控制功能
feature/freertos	最小 FreeRTOS 版本
final_freertos	完整功能 FreeRTOS 版本

如果只想查看功能最完整、结构最接近最终展示版本的代码，可以查看：

final_freertos

⸻

十、后续优化方向

* 将裸机调度进一步迁移到 FreeRTOS 任务模型
* 将电机控制、串口命令、OLED/LED 状态显示拆分为不同任务
* 使用 Queue 解耦 UART 中断和命令解析
* 优化 OLED 刷新逻辑，避免阻塞关键控制流程
* 增加任务栈检查和故障诊断机制
* 后续可扩展编码器测速、PID 闭环控制、电流保护等功能

⸻

十一、项目定位

本项目来源于课程电机控制项目整理与升级，重点用于学习和展示：

* STM32 HAL 外设开发
* PWM 电机控制
* ADC 调速
* 串口通信
* OLED 显示
* CMake 交叉编译
* UART ISP 烧录流程

该项目不是工业级电机控制系统，主要用于嵌入式学习、课程项目整理和简历项目展示。