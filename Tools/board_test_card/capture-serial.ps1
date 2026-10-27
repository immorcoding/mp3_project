# Board serial log capture: append each line with a timestamp until stopped.
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$OutFile,
    [int]$Baud = 115200
)

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
        Add-Content -Path $OutFile -Value "$stamp $line" -Encoding UTF8
    }
    catch [System.TimeoutException] { }
    catch {
        if ($serial.IsOpen) { try { $serial.Close() } catch { } }
        Start-Sleep -Seconds 1
    }
}
