# Resetování konzole do výchozího stavu systému (černé pozadí)
Clear-Host

$VER = "2.0.0"

# Pomocná funkce pro výpis textu
function Write-Menu {
    param([string]$text, [string]$color = "White")
    Write-Host $text -ForegroundColor $color
}

Write-Menu "RadioESP32-$VER flash utility" -color "Yellow"
Write-Menu "==================================================" -color "Yellow"
Write-Menu "- [0] Perform Chip Erase on SPI flash"
Write-Menu "- [1] Flash Complete Firmware"
Write-Menu "- [2] Flash Firmware `"Radio`""
Write-Menu "- [3] Flash Firmware `"Bluetooth loudspeaker`""
Write-Menu "- [4] Flash Firmware `"Update Manager`""
Write-Menu "- [Q] Quit / Exit"
Write-Menu "==================================================" -color "Yellow"
Write-Menu ""
Write-Menu "Note:"
Write-Menu "-----"
Write-Menu "On a new chip (or one used in another application) you need to"
Write-Menu "perform the action `"Flash Complete Firmware`" at least once !"
Write-Menu ""

# Hledání souborů
$menufile = (Get-ChildItem "menu_app\*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
$radiofile = (Get-ChildItem "radio_app\*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
$btlsfile = (Get-ChildItem "btls_app\*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName

# Funkce pro autodetekci portu
function Get-AutodetectedPort {
    Write-Menu "Detecting COM port, please wait..." -color "Yellow"
    $detected = Get-CimInstance Win32_PnPEntity | 
                Where-Object { $_.Caption -match 'Silicon|CH340|USB-SERIAL|Prolific' } | 
                ForEach-Object { if ($_.Caption -match '\((COM\d+)\)') { $Matches[1] } } |
                Select-Object -First 1

    if ($detected) {
        Write-Menu "Autodetected port: $detected" -color "Yellow"
        return $detected
    } else {
        Write-Menu ""
        Write-Host "[ERROR] Could not autodetect any connected ESP32 device!" -ForegroundColor Red
        Write-Menu "        Please make sure the device is connected via USB."
        Write-Menu ""
        Write-Menu "Press any key to exit . . . "
        [void]$Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        Exit
    }
}

# Načtení volby od uživatele
$opt = Read-Host "Please choose an option (eg. 2) and press Enter"

# Ošetření ukončení přes Q
if ($opt.ToUpper() -eq "Q") {
    Write-Menu "`nExiting utility..."
    Start-Sleep -Seconds 1
    Exit
}

# Funkce pro načtení COM portu s autodetekcí
function Get-ComPort {
    $comInput = Read-Host "Enter COM Port eg. `"COM1`", `"COM2`" or `"COM7`" [or press Enter for Autodetect]"
    if ([string]::IsNullOrWhiteSpace($comInput)) {
        return Get-AutodetectedPort
    }
    return $comInput.ToUpper()
}

# Zpracování volby pomocí konstrukce Switch
switch ($opt) {
    "0" {
        Write-Menu "----------------------------------------------`n`nChip Erase" -color "Yellow"
        $com = Get-ComPort
        & .\esptool.exe --chip esp32 --port "$com" --baud 460800 --before default-reset --after hard-reset erase-flash
    }
    "1" {
        Write-Menu "--------------------------------------------------`n`nFlash Complete Firmware" -color "Yellow"
        $com = Get-ComPort
        & .\esptool.exe --chip esp32 --port "$com" --baud 460800 --before default-reset --after hard-reset write-flash -z --flash-mode dout --flash-freq 80m --flash-size 8MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 "$menufile" 0x1d0000 "$radiofile" 0x530000 "$btlsfile"
    }
    "2" {
        Write-Menu "--------------------------------------------------`n`nFlash Firmware `"Radio`"" -color "Yellow"
        $com = Get-ComPort
        & .\esptool.exe --chip esp32 --port "$com" --baud 460800 --before default-reset --after hard-reset write-flash -z --flash-mode dout --flash-freq 80m --flash-size 8MB 0x1d0000 "$radiofile"
    }
    "3" {
        Write-Menu "--------------------------------------------------`n`nFlash Firmware `"Bluetooth loudspeaker`"" -color "Yellow"
        $com = Get-ComPort
        & .\esptool.exe --chip esp32 --port "$com" --baud 460800 --before default-reset --after hard-reset write-flash -z --flash-mode dout --flash-freq 80m --flash-size 8MB 0x530000 "$btlsfile"
    }
    "4" {
        Write-Menu "--------------------------------------------------`n`nFlash Firmware `"Update Manager`"" -color "Yellow"
        $com = Get-ComPort
        & .\esptool.exe --chip esp32 --port "$com" --baud 460800 --before default-reset --after hard-reset write-flash -z --flash-mode dout --flash-freq 80m --flash-size 8MB 0x10000 "$menufile"
    }
    Default {
        Write-Host "Unknown option `"$opt`"" -ForegroundColor Red
        Write-Menu "Press any key to exit . . . "
        [void]$Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        Exit
    }
}

# Společný konec pro úspěšné případy
Write-Menu "`nDone !"
Write-Menu "Press any key to exit . . . "
[void]$Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
