#!/bin/bash
# encode-audio-web.sh <audio_clip.py's folder> [crop w:h:x:y]
# The 3D audio clip's web files from audio_clip.py's master and mixes: the
# video without sound (MP4 and WebM at 1280x720, a keyframe a second) and
# its first frame as a poster, and each mix as Opus (WebM) and AAC (M4A),
# audio-clip-3d (headphones) and audio-clip-stereo (speakers). The mixes are
# first lined up with each other to the sample (the sound both share, the
# player's own and the announcer's, cross-correlated): the recorded offsets
# are counted in the mixer's 10 ms blocks.
set -eu
RUNS=${RUNS:-$HOME/halo-capture-runs}
dir=$RUNS/$1 crop=${2:-}
cd "$dir"; mkdir -p web
python3 - <<'PY'
import wave, numpy as np
def read(name):
    w = wave.open(name)
    return w.getframerate(), np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, 2).astype(np.float64)
rate, h = read('headphones.wav'); _, s = read('speakers.wav')
n = min(len(h), len(s))
a, b = h[:n].sum(1), s[:n].sum(1)
# (the lag of the speakers' mix behind the headphones', within 20 ms)
size = 1 << int(np.ceil(np.log2(2 * n)))
corr = np.fft.irfft(np.fft.rfft(a, size) * np.conj(np.fft.rfft(b, size)), size)
lags = np.r_[corr[:961], corr[-960:]]
lag = int(np.argmax(lags)); lag = lag if lag <= 960 else lag - 1921
print(f'speakers lag {-lag} samples ({-lag / rate * 1000:.2f} ms)')
if lag > 0:
    s = np.vstack([np.zeros((lag, 2)), s])
elif lag < 0:
    s = s[-lag:]
s = np.vstack([s, np.zeros((max(0, len(h) - len(s)), 2))])[:len(h)]
w = wave.open('speakers-aligned.wav', 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
w.writeframes(np.clip(s, -32768, 32767).astype('<i2').tobytes()); w.close()
PY
length=$(ffprobe -v error -show_entries format=duration -of csv=p=0 headphones.wav)
vf="${crop:+crop=$crop,}scale=1280:720:flags=lanczos,format=yuv420p"
ffmpeg -nostdin -loglevel error -y -i master.mp4 -t "$length" -vf "$vf" -an -c:v libx264 -crf ${MP4_CRF:-27} -preset slow -profile:v high -g 60 -movflags +faststart web/audio-clip.mp4
ffmpeg -nostdin -loglevel error -y -i master.mp4 -t "$length" -vf "$vf" -an -c:v libvpx-vp9 -crf ${WEBM_CRF:-40} -b:v 0 -row-mt 1 -g 60 web/audio-clip.webm
ffmpeg -nostdin -loglevel error -y -i web/audio-clip.mp4 -frames:v 1 -c:v libwebp -quality 75 web/audio-clip-poster.webp
for mix in 3d:headphones.wav stereo:speakers-aligned.wav; do
  name=${mix%%:*} wav=${mix#*:}
  ffmpeg -nostdin -loglevel error -y -i "$wav" -c:a libopus -b:a ${OPUS_RATE:-96k} -f webm web/audio-clip-$name.webm
  ffmpeg -nostdin -loglevel error -y -i "$wav" -c:a aac -b:a ${AAC_RATE:-128k} -movflags +faststart web/audio-clip-$name.m4a
done
for f in web/*; do printf '%s %sKB\n' "${f##*/}" $(( $(stat -c%s "$f") / 1024 )); done
