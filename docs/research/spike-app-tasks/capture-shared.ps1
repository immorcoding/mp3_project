# Board serial capture. Same as Tools/board_test_card/capture-serial.ps1, but the log file stays readable while written.
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$OutFile,
    [int]$Baud = 115200
)

$stream = [System.IO.File]::Open($OutFile,
    [System.IO.FileMode]::Append,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::ReadWrite)
$writer = New-Object System.IO.StreamWriter($stream, (New-Object System.Text.UTF8Encoding($false)))
$writer.AutoFlush = $true

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$serial.Encoding = [System.Text.Encoding]::UTF8
$serial.DtrEnable = $true
$serial.ReadTimeout = 500
$serial.NewLine = "`n"

# The CDC port disappears on reset or re-enumeration; retry opening every second.
while ($true) {
    try {
        if (-not $serial.IsOpen) { $serial.Open() }
        $line = $serial.ReadLine().TrimEnd("`r")
        $stamp = (Get-Date).ToString('HH:mm:ss.fff')
        $writer.WriteLine("$stamp $line")
    }
    catch [System.TimeoutException] { }
    catch {
        if ($serial.IsOpen) { try { $serial.Close() } catch { } }
        Start-Sleep -Seconds 1
    }
}
