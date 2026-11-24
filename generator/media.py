from pathlib import Path
import wave
import numpy as np
from scipy.signal import resample_poly

def write_audio(source, destination):
    with wave.open(str(source), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getcomptype(), stream.getnframes()) != (2, 2, 44100, 'NONE', 9666383):
            raise ValueError('Expected the reference stereo 44100 Hz, 16-bit PCM WAV (9666383 sample frames)')
        sound = np.frombuffer(stream.readframes(stream.getnframes()), '<i2').reshape(-1, 2)
        sound = sound.astype(np.float64).mean(axis=1)
    sound = np.clip(np.rint(resample_poly(sound, 320, 441)), -32768, 32767).astype('<i2')
    if len(sound) != 7014156:
        raise ValueError('Unexpected resampled audio length')
    with wave.open(str(destination), 'wb') as output:
        output.setparams((1, 2, 32000, 0, 'NONE', 'not compressed'))
        output.writeframes(sound.tobytes())
    return {'sample_frames': len(sound), 'rate': 32000, 'channels': 1, 'sample_bits': 16}

def write_metadata(destination):
    lines = ['playercollisiontype=1', 'playerraised#=20', 'playerraisedfpscam#=0', 'playerraisedfpsxshift#=0', 'playerraisedfpsyshift#=0', 'playerspeed#=50', 'playertype=1', 'playerlegs(0)=0', 'playerwings(0)=0', 'playerheads(0)=0', 'playeranimates=0', 'playerrollanims=0', 'playerheighttopelvis#=25', 'playersndappear$=""', 'playersnddisappear$=""', 'playersndshoot$="Objects\\09User\\2Player\\badapple\\apple.wav"', 'playersndjump$=""', 'playersndland$=""', 'playersndengine$=""', 'playersndcollision$=""']
    Path(destination).write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
