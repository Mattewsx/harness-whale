# Generate fixed review samples using an installed Windows voice, never a cloud service.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$voiceSynth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $voiceSynth.SelectVoice('Microsoft Huihui Desktop')
    $voiceSynth.Rate = 1
    $voiceSynth.Volume = 100
    $voiceFormat = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
    $voiceLines = @{ done='忙完啦。'; question='帮我选一下？'; error='遇到问题啦，来看看。' }
    foreach ($voiceId in $voiceLines.Keys) {
        $voiceOutput = Join-Path $PSScriptRoot ('audio/voice-' + $voiceId + '-raw.wav')
        $voiceSynth.SetOutputToWaveFile($voiceOutput, $voiceFormat)
        $voiceSynth.Speak($voiceLines[$voiceId])
    }
} finally {
    $voiceSynth.Dispose()
}
