from pathlib import Path
import argparse
import concurrent.futures
import json
import shutil
import sys
import wave
sys.dont_write_bytecode = True
from pipeline import run_cpp, sha256
from geometry import filter_cache
from model import write_model
from media import write_audio, write_metadata
ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--audio', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True, help='Directory containing the two compiled C++ helpers')
    parser.add_argument('--work', type=Path, required=True, help='Working files, logs and content-checked caches')
    parser.add_argument('--output', type=Path, required=True, help='New or empty output directory')
    args = parser.parse_args()
    video, audio, tools, work, output = (getattr(args, name).resolve() for name in ('video', 'audio', 'tools', 'work', 'output'))
    if sys.platform != 'win32':
        parser.error('The reference MP4 extraction path requires Windows Media Foundation')
    if not video.is_file() or video.suffix.lower() != '.mp4' or (not audio.is_file()):
        parser.error('Supply the reference MP4 and separate PCM WAV')
    with wave.open(str(audio), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getcomptype(), stream.getnframes()) != (2, 2, 44100, 'NONE', 9666383):
            parser.error('WAV must be stereo 44100 Hz, 16-bit PCM, 9666383 sample frames')
    extract, geometry = (tools / 'badapple_extract.exe', tools / 'badapple_geometry.exe')
    if not extract.is_file() or not geometry.is_file():
        parser.error('Build both C++ helpers first')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Output must be a new or empty directory; existing results are preserved')
    if work == output or work.is_relative_to(output) or output.is_relative_to(work):
        parser.error('Use separate work and output directories')
    work.mkdir(parents=True, exist_ok=True)
    masks = work / 'source-masks.bin'
    run_cpp(work, 'extract', extract, [video, masks], [video], [masks])

    def fit(profile):
        cache, report = (work / (profile + '.bin'), work / (profile + '-fit.json'))
        run_cpp(work, 'fit-' + profile, geometry, ['fit', masks, profile, cache, report], [masks], [cache, report])
        run_cpp(work, 'raster-' + profile, geometry, ['raster', cache, work / (profile + '-masks.bin')], [cache], [work / (profile + '-masks.bin')])
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fit, ('reference', 'refined', 'final')))
    print('Checking the refined reference', flush=True)
    refined = work / 'accepted-reference.bin'
    first = filter_cache(masks, work / 'reference.bin', work / 'refined.bin', work / 'reference-masks.bin', work / 'refined-masks.bin', refined, work / 'reference-filter.json')
    refined_masks = work / 'accepted-reference-masks.bin'
    run_cpp(work, 'raster-accepted-reference', geometry, ['raster', refined, refined_masks], [refined], [refined_masks])
    print('Checking the final simplification', flush=True)
    accepted = work / 'accepted-final.bin'
    final = filter_cache(masks, refined, work / 'final.bin', refined_masks, work / 'final-masks.bin', accepted, work / 'final-filter.json', relaxed=True)
    folder = output / 'Objects' / '09User' / '2Player' / 'badapple'
    folder.mkdir(parents=True)
    info = write_model(accepted, work / 'model-text.x', work / 'model-decoded.x', folder / 'badapple.x')
    info['audio'] = write_audio(audio, folder / 'apple.wav')
    write_metadata(folder / 'model.dat')
    shutil.copyfile(ROOT / 'artwork' / 'thumb.bmp', folder / 'thumb.bmp')
    info.update(reference_fallbacks=first['reference_fallbacks'], final_fallbacks=final['reference_fallbacks'], source_video_sha256=sha256(video), source_audio_sha256=sha256(audio), source_masks_sha256=sha256(masks), geometry_sha256=sha256(accepted), files={path.name: sha256(path) for path in sorted(folder.iterdir())})
    (output / 'generation.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    print('Generated ' + str(folder), flush=True)
if __name__ == '__main__':
    main()
