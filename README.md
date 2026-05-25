# motor-clean

## 先看最短用法

当前这个项目，平时只需要记住两条命令：

```bash
./scripts/build.sh
./scripts/flash-isp.sh
```

第一条是编译，第二条是烧录。

如果烧录串口不是 `/dev/cu.usbserial-1140`，就这样用：

```bash
ISP_PORT=/dev/cu.usbserial-XXXX ./scripts/flash-isp.sh
```

如果只是想检查 hex 文件对不对，不真正烧录：

```bash
./scripts/flash-isp.sh --parse-only
```

## 第一次使用怎么做

1. 先确认电脑里有 `cmake`、`ninja`、`arm-none-eabi-gcc`。
2. 再确认 Python 能导入 `pyserial`。
3. 然后执行：

```bash
./scripts/build.sh
./scripts/flash-isp.sh
```

如果缺 `pyserial`，安装：

```bash
python3 -m pip install pyserial
```

## 哪个文件要改

项目默认配置都在 [scripts/project.env](/Users/pap/Documents/软件工作区/Codex/motor-clean/scripts/project.env)。

大多数情况下，你只需要关心这几个值：

```bash
PROJECT_NAME=416
ISP_PORT=/dev/cu.usbserial-1140
ISP_BAUD=115200
```

它们的意思是：

- `PROJECT_NAME`：编译后产物的名字。现在会生成 `build/416.elf`、`build/416.hex` 这些文件。
- `ISP_PORT`：烧录串口。
- `ISP_BAUD`：烧录波特率。

如果你只是这台电脑换了串口，通常不用改文件，直接临时覆盖就够了：

```bash
ISP_PORT=/dev/cu.usbserial-XXXX ./scripts/flash-isp.sh
```

## 这两个脚本到底做什么

- [scripts/build.sh](/Users/pap/Documents/软件工作区/Codex/motor-clean/scripts/build.sh) 会读取 `scripts/project.env`，然后用 CMake + Ninja 编译工程，最后检查 `elf`、`hex`、`bin`、`map` 是否生成。
- [scripts/flash-isp.sh](/Users/pap/Documents/软件工作区/Codex/motor-clean/scripts/flash-isp.sh) 会读取 `scripts/project.env`，找到要烧录的 `hex` 文件，再调用 [scripts/flash_isp.py](/Users/pap/Documents/软件工作区/Codex/motor-clean/scripts/flash_isp.py) 通过 STM32 UART ISP 下载。

## 搬到新项目怎么复用

适用范围是：STM32 + CMake + Ninja + `arm-none-eabi-gcc`，并且仍然走 UART ISP 烧录。

把下面 4 个文件复制到新项目里：

```text
scripts/build.sh
scripts/flash-isp.sh
scripts/flash_isp.py
scripts/project.env
```

然后只做 3 件事：

1. 把 `PROJECT_NAME` 改成新项目的目标名。
2. 把 `CMAKE_TOOLCHAIN_FILE` 改成新项目实际的 toolchain 文件路径。
3. 把 `ISP_PORT` 改成新板子的串口。

最常改的是这几行：

```bash
PROJECT_NAME=my_firmware
BUILD_DIR=build
BUILD_TYPE=Release
CMAKE_GENERATOR=Ninja
CMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake

ISP_HEX="$BUILD_DIR/$PROJECT_NAME.hex"
ISP_PORT=/dev/cu.usbserial-XXXX
ISP_BAUD=115200
```

改完后先试：

```bash
./scripts/build.sh
./scripts/flash-isp.sh --parse-only
```

如果 `--parse-only` 能正常读到 hex，再去真实烧录。

## 常见情况直接抄

Debug 且不删旧的 `build` 目录：

```bash
BUILD_TYPE=Debug CLEAN_BUILD=0 ./scripts/build.sh
```

使用另一份配置文件：

```bash
PROJECT_CONFIG=/path/to/project.env ./scripts/build.sh
```

手动直接调用 Python 烧录工具：

```bash
python3 scripts/flash_isp.py \
  --hex build/416.hex \
  --port /dev/cu.usbserial-1140 \
  --baud 115200
```

## 遇到问题先看这里

`./scripts/build.sh` 报找不到工具：
说明 `cmake`、`ninja` 或 `arm-none-eabi-gcc` 还没装好，或者不在 `PATH` 里。

`./scripts/flash-isp.sh` 报找不到 hex：
先执行一次 `./scripts/build.sh`。

烧录时报串口打不开：
先检查 `ISP_PORT` 写得对不对。

换到新项目后，构建产物名字不对：
先检查 `PROJECT_NAME` 是否和 `CMakeLists.txt` 里的目标名一致。

如果想保留 build 目录做增量编译：

```bash
CLEAN_BUILD=0 ./scripts/build.sh
```
