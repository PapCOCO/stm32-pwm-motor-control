#!/usr/bin/env python3
"""Flash STM32 firmware over the built-in UART ISP bootloader."""

from __future__ import annotations

import argparse
import os
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List


ACK = 0x79
NACK = 0x1F
SYNC = 0x7F


class FlashError(RuntimeError):
    """User-facing flashing error."""


@dataclass(frozen=True)
class Segment:
    address: int
    data: bytes


def xor_checksum(data: Iterable[int]) -> int:
    value = 0
    for byte in data:
        value ^= byte
    return value & 0xFF


def parse_hex_byte(text: str, line_no: int) -> int:
    try:
        return int(text, 16)
    except ValueError as exc:
        raise FlashError(f"HEX parse error on line {line_no}: invalid hex byte") from exc


def read_intel_hex(path: str) -> Dict[int, int]:
    if not os.path.exists(path):
        raise FlashError(f"HEX 不存在: {path}")
    if not os.path.isfile(path):
        raise FlashError(f"HEX 路径不是文件: {path}")

    memory: Dict[int, int] = {}
    base = 0

    with open(path, "r", encoding="ascii") as hex_file:
        for line_no, raw_line in enumerate(hex_file, start=1):
            line = raw_line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise FlashError(f"HEX parse error on line {line_no}: missing ':'")
            if len(line) < 11 or (len(line) - 1) % 2 != 0:
                raise FlashError(f"HEX parse error on line {line_no}: bad record length")

            record = bytes(
                parse_hex_byte(line[index : index + 2], line_no)
                for index in range(1, len(line), 2)
            )
            count = record[0]
            if len(record) != count + 5:
                raise FlashError(f"HEX parse error on line {line_no}: byte count mismatch")
            if (sum(record) & 0xFF) != 0:
                raise FlashError(f"HEX parse error on line {line_no}: checksum mismatch")

            offset = (record[1] << 8) | record[2]
            record_type = record[3]
            data = record[4 : 4 + count]

            if record_type == 0x00:
                for index, byte in enumerate(data):
                    memory[base + offset + index] = byte
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                if count != 2:
                    raise FlashError(f"HEX parse error on line {line_no}: bad segment base")
                base = (((data[0] << 8) | data[1]) << 4) & 0xFFFFFFFF
            elif record_type == 0x04:
                if count != 2:
                    raise FlashError(f"HEX parse error on line {line_no}: bad linear base")
                base = (((data[0] << 8) | data[1]) << 16) & 0xFFFFFFFF
            elif record_type in (0x03, 0x05):
                continue
            else:
                raise FlashError(f"HEX parse error on line {line_no}: unsupported record type 0x{record_type:02X}")

    if not memory:
        raise FlashError("HEX 文件不包含可写入数据")

    return memory


def get_segments(memory: Dict[int, int]) -> List[Segment]:
    segments: List[Segment] = []
    start = None
    last = None
    data = bytearray()

    for address in sorted(memory):
        if start is None or last is None or address != last + 1:
            if start is not None:
                segments.append(Segment(start, bytes(data)))
            start = address
            data = bytearray()
        data.append(memory[address])
        last = address

    if start is not None:
        segments.append(Segment(start, bytes(data)))

    return segments


def import_serial_module():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise FlashError("缺少 pyserial，请先安装: python3 -m pip install pyserial") from exc
    return serial


def level_enabled(level: str) -> bool:
    return level == "High"


def opposite_level(level: str) -> str:
    return "Low" if level == "High" else "High"


def set_line_level(port, line: str, level: str) -> None:
    enabled = level_enabled(level)
    if line == "DTR":
        port.dtr = enabled
    elif line == "RTS":
        port.rts = enabled
    else:
        raise ValueError(f"Unsupported line: {line}")


def read_ack(port, timeout: float = 1.0) -> bool:
    old_timeout = port.timeout
    port.timeout = timeout
    try:
        data = port.read(1)
    finally:
        port.timeout = old_timeout

    if not data:
        return False
    value = data[0]
    if value == ACK:
        return True
    if value == NACK:
        return False
    return False


def write_bytes(port, data: bytes) -> None:
    port.write(data)
    port.flush()


def send_command(port, command: int, timeout: float = 1.0) -> bool:
    write_bytes(port, bytes((command, command ^ 0xFF)))
    return read_ack(port, timeout)


def invoke_auto_boot(port, reset_assert_dtr: str, boot_assert_rts: str) -> None:
    reset_release = opposite_level(reset_assert_dtr)
    print(f"Auto boot: ON, DTR={reset_assert_dtr} reset, RTS={boot_assert_rts} bootloader")
    set_line_level(port, "DTR", reset_assert_dtr)
    set_line_level(port, "RTS", boot_assert_rts)
    time.sleep(0.150)
    set_line_level(port, "DTR", reset_release)
    time.sleep(0.250)


def wait_bootloader_sync(port, retries: int = 12) -> None:
    for attempt in range(1, retries + 1):
        port.reset_input_buffer()
        write_bytes(port, bytes((SYNC,)))
        if read_ack(port, 0.5):
            print("Bootloader sync OK")
            return
        print(f"Sync retry {attempt}/{retries}")
        time.sleep(0.200)
    raise FlashError("无法同步 Bootloader: 请确认 BOOT0/RTS 电平、复位时序和串口连接")


def mass_erase(port) -> None:
    print("Erasing flash...")
    if send_command(port, 0x43, 1.0):
        write_bytes(port, bytes((0xFF, 0x00)))
        if read_ack(port, 20.0):
            print("Erase OK")
            return

    print("Standard erase failed, trying extended erase...")
    if send_command(port, 0x44, 1.0):
        write_bytes(port, bytes((0xFF, 0xFF, 0x00)))
        if read_ack(port, 30.0):
            print("Erase OK")
            return

    raise FlashError("擦除失败: Mass Erase 和 Extended Erase 均未收到 ACK")


def send_address(port, address: int) -> None:
    address_bytes = address.to_bytes(4, byteorder="big", signed=False)
    write_bytes(port, address_bytes + bytes((xor_checksum(address_bytes),)))
    if not read_ack(port, 1.0):
        raise FlashError(f"写入失败: 地址包被拒绝 0x{address:08X}")


def write_memory_chunk(port, address: int, data: bytes) -> None:
    if not 1 <= len(data) <= 256:
        raise FlashError("写入失败: 每块数据长度必须为 1..256 bytes")
    if not send_command(port, 0x31, 1.0):
        raise FlashError(f"写入失败: Write Memory 命令被拒绝 0x{address:08X}")

    send_address(port, address)

    packet_without_checksum = bytes((len(data) - 1,)) + data
    packet = packet_without_checksum + bytes((xor_checksum(packet_without_checksum),))
    write_bytes(port, packet)
    if not read_ack(port, 2.0):
        raise FlashError(f"写入失败: 数据包未确认 0x{address:08X}")


def print_progress(written: int, total: int) -> None:
    percent = (written * 100.0) / total if total else 100.0
    print(f"\rWriting: {written}/{total} bytes ({percent:5.1f}%)", end="", flush=True)


def flash(args, segments: List[Segment], total_bytes: int) -> None:
    if not os.path.exists(args.port):
        raise FlashError(f"串口不存在: {args.port}")

    serial = import_serial_module()
    try:
        port = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_EVEN,
            stopbits=serial.STOPBITS_ONE,
            timeout=1.0,
            write_timeout=1.0,
        )
    except serial.SerialException as exc:
        raise FlashError(f"无法打开串口 {args.port}: {exc}") from exc

    try:
        if args.no_auto_boot:
            print("Auto boot: OFF, manual BOOT0=1 + RESET required")
            time.sleep(2.0)
        else:
            invoke_auto_boot(port, args.reset_assert_dtr, args.boot_assert_rts)

        wait_bootloader_sync(port)

        if args.no_erase:
            print("Erase skipped")
        else:
            mass_erase(port)

        written = 0
        print_progress(written, total_bytes)
        for segment in segments:
            for offset in range(0, len(segment.data), 256):
                chunk = segment.data[offset : offset + 256]
                write_memory_chunk(port, segment.address + offset, chunk)
                written += len(chunk)
                print_progress(written, total_bytes)
        print()
        print(f"Download OK: wrote {written} bytes")
    finally:
        port.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Flash STM32 over UART ISP bootloader.")
    parser.add_argument("--hex", default="build/416.hex", help="Intel HEX file path")
    parser.add_argument("--port", default="/dev/cu.usbserial-1140", help="Serial port path")
    parser.add_argument("--baud", default=115200, type=int, help="Serial baud rate")
    parser.add_argument("--reset-assert-dtr", choices=("Low", "High"), default="Low")
    parser.add_argument("--boot-assert-rts", choices=("Low", "High"), default="High")
    parser.add_argument("--no-auto-boot", action="store_true", help="Do not control DTR/RTS")
    parser.add_argument("--no-erase", action="store_true", help="Skip flash erase")
    parser.add_argument("--parse-only", action="store_true", help="Only parse HEX and print segments")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        hex_path = os.path.abspath(args.hex)
        memory = read_intel_hex(hex_path)
        segments = get_segments(memory)
        total_bytes = len(memory)

        print(f"HEX: {hex_path}")
        print(f"Port: {args.port}, baud: {args.baud}")
        print(f"Data bytes: {total_bytes}")
        print(f"Auto boot: {'OFF' if args.no_auto_boot else 'ON'}")

        if args.parse_only:
            for segment in segments:
                print(f"Segment: 0x{segment.address:08X}, {len(segment.data)} bytes")
            print("Parse OK")
            return 0

        flash(args, segments, total_bytes)
        return 0
    except FlashError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nError: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
