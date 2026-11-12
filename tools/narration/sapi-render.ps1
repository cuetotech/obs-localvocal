param(
    [Parameter(Mandatory=$true)][string]$InputFile,
    [Parameter(Mandatory=$true)][string]$WavFile,
    [string]$Voice = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Speech
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    if ($Voice) { $speaker.SelectVoice($Voice) }
    $speaker.SetOutputToWaveFile($WavFile)
    $text = [System.IO.File]::ReadAllText($InputFile, [System.Text.Encoding]::UTF8)
    $speaker.Speak($text)
} finally {
    $speaker.Dispose()
}
