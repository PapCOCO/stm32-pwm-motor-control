param(
    [string]$HexPath = "build/Release/416.hex",
    [string]$PortName = "COM9",
    [int]$BaudRate = 115200,
    [ValidateSet("Low", "High")]
    [string]$ResetAssertDtr = "Low",
    [ValidateSet("Low", "High")]
    [string]$BootAssertRts = "High",
    [switch]$NoAutoBoot,
    [switch]$NoErase,
    [switch]$ParseOnly
)

$ErrorActionPreference = "Stop"

function New-ByteArray {
    param([int[]]$Values)
    $data = [byte[]]::new($Values.Count)
    for ($i = 0; $i -lt $Values.Count; $i++) {
        $data[$i] = [byte]($Values[$i] -band 0xFF)
    }
    return $data
}

function Get-Xor {
    param([byte[]]$Data)
    [byte]$sum = 0
    foreach ($b in $Data) {
        $sum = [byte](($sum -bxor $b) -band 0xFF)
    }
    return $sum
}

function Write-SerialBytes {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [byte[]]$Data
    )
    $Port.Write($Data, 0, $Data.Length)
}

function Read-Ack {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$TimeoutMs = 1000
    )
    $oldTimeout = $Port.ReadTimeout
    $Port.ReadTimeout = $TimeoutMs
    try {
        $value = $Port.ReadByte()
    } catch [System.TimeoutException] {
        return $false
    } finally {
        $Port.ReadTimeout = $oldTimeout
    }

    if ($value -eq 0x79) {
        return $true
    }
    if ($value -eq 0x1F) {
        return $false
    }
    return $false
}

function Send-Command {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$Command,
        [int]$TimeoutMs = 1000
    )
    Write-SerialBytes $Port (New-ByteArray @($Command, ($Command -bxor 0xFF)))
    return (Read-Ack $Port $TimeoutMs)
}

function Set-LineLevel {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [ValidateSet("DTR", "RTS")]
        [string]$Line,
        [ValidateSet("Low", "High")]
        [string]$Level
    )
    $enabled = ($Level -eq "High")
    if ($Line -eq "DTR") {
        $Port.DtrEnable = $enabled
    } else {
        $Port.RtsEnable = $enabled
    }
}

function Invoke-AutoBoot {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$ResetAssertDtr,
        [string]$BootAssertRts
    )
    $resetRelease = if ($ResetAssertDtr -eq "Low") { "High" } else { "Low" }

    Write-Host "Auto boot: DTR=$ResetAssertDtr reset, RTS=$BootAssertRts bootloader"
    Set-LineLevel $Port "DTR" $ResetAssertDtr
    Set-LineLevel $Port "RTS" $BootAssertRts
    Start-Sleep -Milliseconds 150
    Set-LineLevel $Port "DTR" $resetRelease
    Start-Sleep -Milliseconds 250
}

function Wait-BootloaderSync {
    param([System.IO.Ports.SerialPort]$Port)
    for ($i = 1; $i -le 12; $i++) {
        $Port.DiscardInBuffer()
        Write-SerialBytes $Port (New-ByteArray @(0x7F))
        if (Read-Ack $Port 500) {
            Write-Host "Bootloader sync OK"
            return
        }
        Write-Host "Sync retry $i/12"
        Start-Sleep -Milliseconds 200
    }
    throw "Could not sync with STM32 bootloader. Put BOOT0=1 and reset the board, or adjust DTR/RTS levels."
}

function Invoke-MassErase {
    param([System.IO.Ports.SerialPort]$Port)

    Write-Host "Erasing flash..."
    if (Send-Command $Port 0x43 1000) {
        Write-SerialBytes $Port (New-ByteArray @(0xFF, 0x00))
        if (Read-Ack $Port 20000) {
            Write-Host "Erase OK"
            return
        }
    }

    Write-Host "Standard erase failed, trying extended erase..."
    if (Send-Command $Port 0x44 1000) {
        Write-SerialBytes $Port (New-ByteArray @(0xFF, 0xFF, 0x00))
        if (Read-Ack $Port 30000) {
            Write-Host "Extended erase OK"
            return
        }
    }

    throw "Flash erase failed."
}

function Send-Address {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [UInt32]$Address
    )
    $bytes = New-ByteArray @(
        (($Address -shr 24) -band 0xFF),
        (($Address -shr 16) -band 0xFF),
        (($Address -shr 8) -band 0xFF),
        ($Address -band 0xFF)
    )
    $packet = [byte[]]::new(5)
    [Array]::Copy($bytes, 0, $packet, 0, 4)
    $packet[4] = Get-Xor $bytes
    Write-SerialBytes $Port $packet
    if (-not (Read-Ack $Port 1000)) {
        throw ("Address rejected: 0x{0:X8}" -f $Address)
    }
}

function Write-MemoryChunk {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [UInt32]$Address,
        [byte[]]$Data
    )
    if ($Data.Length -lt 1 -or $Data.Length -gt 256) {
        throw "Write chunk must be 1..256 bytes."
    }
    if (-not (Send-Command $Port 0x31 1000)) {
        throw ("Write command rejected at 0x{0:X8}" -f $Address)
    }
    Send-Address $Port $Address

    $packet = [byte[]]::new($Data.Length + 2)
    $packet[0] = [byte]($Data.Length - 1)
    [Array]::Copy($Data, 0, $packet, 1, $Data.Length)
    $packet[$packet.Length - 1] = Get-Xor $packet[0..($packet.Length - 2)]

    Write-SerialBytes $Port $packet
    if (-not (Read-Ack $Port 2000)) {
        throw ("Write failed at 0x{0:X8}" -f $Address)
    }
}

function Read-IntelHex {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "HEX file not found: $Path"
    }

    $memory = [System.Collections.Generic.SortedDictionary[UInt32, byte]]::new()
    [UInt32]$base = 0

    foreach ($line in Get-Content -LiteralPath $Path) {
        if (-not $line.StartsWith(":")) {
            continue
        }

        $count = [Convert]::ToInt32($line.Substring(1, 2), 16)
        $offset = [Convert]::ToUInt32($line.Substring(3, 4), 16)
        $type = [Convert]::ToInt32($line.Substring(7, 2), 16)
        $bytes = [byte[]]::new($count)
        for ($i = 0; $i -lt $count; $i++) {
            $bytes[$i] = [Convert]::ToByte($line.Substring(9 + ($i * 2), 2), 16)
        }

        if ($type -eq 0x00) {
            for ($i = 0; $i -lt $count; $i++) {
                [UInt32]$address = $base + $offset + [UInt32]$i
                $memory[$address] = $bytes[$i]
            }
        } elseif ($type -eq 0x01) {
            break
        } elseif ($type -eq 0x04) {
            $base = [UInt32](([UInt32]$bytes[0] -shl 24) -bor ([UInt32]$bytes[1] -shl 16))
        } elseif ($type -eq 0x02) {
            $base = [UInt32]((([UInt32]$bytes[0] -shl 8) -bor [UInt32]$bytes[1]) -shl 4)
        }
    }

    if ($memory.Count -eq 0) {
        throw "HEX file contains no data."
    }

    return $memory
}

function Get-ContiguousSegments {
    param([System.Collections.Generic.SortedDictionary[UInt32, byte]]$Memory)

    $segments = [System.Collections.Generic.List[object]]::new()
    [bool]$hasSegment = $false
    [UInt32]$start = 0
    [UInt32]$last = 0
    $bytes = [System.Collections.Generic.List[byte]]::new()

    foreach ($entry in $Memory.GetEnumerator()) {
        [UInt32]$addr = $entry.Key
        if (-not $hasSegment -or $addr -ne ($last + 1)) {
            if ($hasSegment) {
                $segments.Add([pscustomobject]@{
                    Address = $start
                    Data = $bytes.ToArray()
                })
            }
            $start = $addr
            $bytes = [System.Collections.Generic.List[byte]]::new()
            $hasSegment = $true
        }
        $bytes.Add($entry.Value)
        $last = $addr
    }

    if ($hasSegment) {
        $segments.Add([pscustomobject]@{
            Address = $start
            Data = $bytes.ToArray()
        })
    }

    return $segments.ToArray()
}

$resolvedHex = (Resolve-Path -LiteralPath $HexPath).Path
$memory = Read-IntelHex $resolvedHex
$segments = Get-ContiguousSegments $memory
$totalBytes = $memory.Count

Write-Host "HEX: $resolvedHex"
Write-Host "Port: $PortName, baud: $BaudRate"
Write-Host "Data bytes: $totalBytes"

if ($ParseOnly) {
    foreach ($segment in $segments) {
        Write-Host ("Segment: 0x{0:X8}, {1} bytes" -f $segment.Address, $segment.Data.Length)
    }
    Write-Host "Parse OK"
    exit 0
}

$serial = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 1000
$serial.WriteTimeout = 1000

try {
    $serial.Open()
    if (-not $NoAutoBoot) {
        Invoke-AutoBoot $serial $ResetAssertDtr $BootAssertRts
    } else {
        Write-Host "Manual mode: set BOOT0=1 and press RESET now."
        Start-Sleep -Seconds 2
    }

    Wait-BootloaderSync $serial

    if (-not $NoErase) {
        Invoke-MassErase $serial
    }

    $written = 0
    foreach ($segment in $segments) {
        [UInt32]$address = $segment.Address
        [byte[]]$data = $segment.Data
        for ($offset = 0; $offset -lt $data.Length; $offset += 256) {
            $len = [Math]::Min(256, $data.Length - $offset)
            $chunk = [byte[]]::new($len)
            [Array]::Copy($data, $offset, $chunk, 0, $len)
            Write-MemoryChunk $serial ([UInt32]($address + $offset)) $chunk
            $written += $len
            Write-Progress -Activity "Writing flash" -Status "$written / $totalBytes bytes" -PercentComplete (($written * 100) / $totalBytes)
        }
    }
    Write-Progress -Activity "Writing flash" -Completed
    Write-Host "Download OK: wrote $written bytes"
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
