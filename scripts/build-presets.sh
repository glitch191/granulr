#!/usr/bin/env bash
# Generates the factory presets in presets/factory from the compact definitions below.
# Usage: bash scripts/build-presets.sh
#
# preset "Name" "Category" "paramId=value ..." "source:destParamId:depth ..."
# Only parameters that differ from their default are listed. Values use the parameter's
# own unit (ms, %, st, Hz, dB). Choices are indices, switches are 0 or 1.
# Sources: 0-3 LFO 1-4, 4-5 Random 1-2, 6 Envelope, 7-10 Macro 1-4.
set -euo pipefail
out="$(cd "$(dirname "$0")/.." && pwd)/presets/factory"
mkdir -p "$out"
rm -f "$out"/*.xml

preset() {
  local name="$1" category="$2" params="$3" routes="${4:-}"
  local file="$out/$(echo "$category-$name" | tr ' ' '_' | tr -cd 'A-Za-z0-9_-').xml"
  {
    printf '<?xml version="1.0" encoding="UTF-8"?>\n'
    printf '<GranulrPreset name="%s" category="%s" formatVersion="1">\n' "$name" "$category"
    printf '  <PARAMETERS>\n'
    for pair in $params; do
      printf '    <PARAM id="%s" value="%s"/>\n' "${pair%%=*}" "${pair#*=}"
    done
    printf '  </PARAMETERS>\n  <MATRIX>\n'
    for route in $routes; do
      IFS=: read -r source dest depth <<< "$route"
      printf '    <SLOT source="%s" dest="%s" depth="%s"/>\n' "$source" "$dest" "$depth"
    done
    printf '  </MATRIX>\n</GranulrPreset>\n'
  } > "$file"
}

preset "Slow Clouds" "Ambient" \
  "mix=60 size=450 density=14 spray=600 position=400 jitter=60 panSpread=80 feedback=30 pitchRandom=0.1 diffOn=1 diffMix=45 diffDecay=70 diffSize=70 filterOn=1 filterCutoff=6000 lfo1Rate=0.08 rand1Rate=0.5 rand1Smooth=80" \
  "0:position:0.15 4:panSpread:-0.2 7:diffMix:0.4 7:feedback:0.3 8:size:0.4"

preset "Sixteenth Shards" "Glitch" \
  "mix=55 triggerMode=2 patternRate=5 size=70 positionSync=1 positionDiv=8 reverse=30 windowShape=3 spray=0 jitter=0 octaveUp=20 satOn=1 satDrive=9 panSpread=60 step3Gate=0 step7Gate=0 step11Gate=0 step4Ratchet=2 step8Ratchet=3 step12Ratchet=2 step16Ratchet=4 step6Prob=60 step14Prob=50" \
  "7:reverse:0.6 8:size:0.35 4:panSpread:0.3"

preset "Octave Halo" "Shimmer" \
  "mix=45 pitch=12 feedback=62 size=220 density=30 spray=150 position=180 panSpread=70 duckAmount=25 diffOn=1 diffPlace=0 diffMix=60 diffDecay=75 diffSize=65 filterOn=1 filterPlace=0 filterCutoff=7000 lfo1Rate=0.15" \
  "7:feedback:0.25 7:diffMix:0.3 0:position:0.1 8:filterCutoff:-0.5"

preset "Held Breath" "Frozen" \
  "mix=80 freeze=1 size=600 density=18 spray=1500 position=1000 scan=0.15 pitchRandom=0.05 panSpread=90 jitter=70 diffOn=1 diffMix=50 diffDecay=85 diffSize=80 lfo1Rate=0.05 lfo2Rate=0.11" \
  "0:position:0.2 1:size:0.2 7:scan:0.25 8:pitch:0.25"

preset "Grain Echo" "Delay" \
  "mix=40 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=10 sizeSync=1 sizeDiv=8 feedback=55 spray=0 jitter=0 panSpread=40 windowShape=1 filterOn=1 filterPlace=0 filterCutoff=4500" \
  "7:feedback:0.4 8:filterCutoff:0.4"

preset "Thickener" "Chorus" \
  "mix=50 size=45 density=60 position=25 spray=12 pitchRandom=0.12 panSpread=100 width=140 jitter=80 lfo1Rate=0.3 lfo2Rate=0.47" \
  "0:fine:0.08 1:position:0.02 7:width:0.25 7:pitchRandom:0.1"

preset "Backward Swell" "Reverse" \
  "mix=60 reverse=100 size=900 density=4 windowSkew=70 position=900 spray=100 panSpread=50 feedback=15 diffOn=1 diffMix=30 diffDecay=60" \
  "7:size:0.3 8:diffMix:0.5 4:panSpread:0.3"

preset "Pentatonic Stairs" "Cascades" \
  "mix=55 triggerMode=2 patternRate=5 scale=5 root=9 pitchRandom=14 size=160 octaveUp=30 position=180 feedback=35 windowShape=3 panSpread=75 diffOn=1 diffMix=25 step2Prob=70 step5Prob=60 step10Prob=75 step13Ratchet=2 step15Prob=50 step16Ratchet=3" \
  "7:pitchRandom:0.4 8:feedback:0.4 6:octaveUp:0.4"

preset "Worn Tape" "Lo-Fi" \
  "mix=100 size=120 density=35 position=60 spray=8 pitchRandom=0.08 panSpread=10 width=80 jitter=30 satOn=1 satDrive=14 filterOn=1 filterCutoff=3200 filterReso=15 lfo1Shape=1 lfo1Rate=0.7 rand1Rate=6 rand1Smooth=80" \
  "0:fine:0.12 4:fine:0.05 7:satDrive:0.4 8:filterCutoff:0.35"

preset "Behind The Voice" "Ducked" \
  "mix=50 duckAmount=85 duckAttack=5 duckRelease=400 size=350 density=22 spray=500 position=300 octaveUp=25 feedback=40 panSpread=85 diffOn=1 diffMix=55 diffDecay=65" \
  "7:duckAmount:-0.5 8:diffMix:0.4 0:position:0.1"

preset "Transient Blur" "Smear" \
  "mix=65 size=90 density=110 position=35 spray=70 jitter=100 panSpread=60 diffOn=1 diffSize=20 diffDecay=20 diffMix=40" \
  "7:spray:0.35 7:size:0.25 6:density:-0.3"

preset "Short Source Drone" "Drones" \
  "mix=85 size=1400 density=9 position=200 spray=180 feedback=78 pitch=-12 voice2Prob=40 voice3Prob=30 panSpread=70 diffOn=1 diffPlace=0 diffMix=50 diffDecay=90 filterOn=1 filterPlace=0 filterCutoff=2400 lfo1Rate=0.04" \
  "0:filterCutoff:0.2 7:feedback:0.2 8:pitchRandom:0.3"

echo "Wrote $(ls "$out"/*.xml | wc -l) presets to $out"
