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
  "outputGain=-4 mix=85 size=1400 density=9 position=200 spray=180 feedback=68 pitch=-12 voice2Prob=40 voice3Prob=30 panSpread=70 diffOn=1 diffPlace=0 diffMix=50 diffDecay=90 filterOn=1 filterPlace=0 filterCutoff=2400 lfo1Rate=0.04" \
  "0:filterCutoff:0.2 7:feedback:0.2 8:pitchRandom:0.3"

# ---------------------------------------------------------------------------------------------
# Ambient

preset "Morning Mist" "Ambient" \
  "mix=55 size=700 density=10 spray=900 position=600 jitter=80 panSpread=90 pitchRandom=0.06 feedback=20 diffOn=1 diffMix=55 diffDecay=80 diffSize=85 diffDamp=45 lfo1Rate=0.05" \
  "0:position:0.2 1:size:0.15 7:diffMix:0.35 8:spray:0.3"

preset "Glass Garden" "Ambient" \
  "mix=50 size=180 density=26 spray=700 position=500 octaveUp=35 scale=4 pitchRandom=7 panSpread=100 diffOn=1 diffMix=45 diffDecay=70 filterOn=1 filterType=1 filterCutoff=400" \
  "4:panSpread:-0.3 7:octaveUp:0.4 8:pitchRandom:0.3"

preset "Tidal Drift" "Ambient" \
  "mix=60 size=900 density=6 spray=1200 position=1500 panSpread=85 feedback=25 diffOn=1 diffMix=40 diffDecay=75 lfo1Rate=0.03 lfo2Rate=0.07" \
  "0:position:0.3 1:panSpread:0.2 7:size:0.3 8:feedback:0.4"

preset "Dust In Light" "Ambient" \
  "mix=45 size=35 density=45 spray=1500 position=800 jitter=100 panSpread=100 windowShape=1 pitchRandom=0.2 octaveUp=15 diffOn=1 diffMix=50 diffDecay=65 filterOn=1 filterType=1 filterCutoff=900" \
  "4:density:0.2 7:density:0.3 8:octaveUp:0.5"

preset "Wide Horizon" "Ambient" \
  "mix=55 size=500 density=16 spray=400 position=350 width=170 panSpread=100 voice2Prob=30 voice3Prob=20 feedback=30 diffOn=1 diffMix=50 diffDecay=78 diffSize=75" \
  "0:width:0.1 7:voice2Prob:0.4 8:diffDecay:0.2"

preset "Low Fog" "Ambient" \
  "mix=55 pitch=-12 size=800 density=8 spray=500 position=450 panSpread=60 feedback=35 filterOn=1 filterCutoff=1800 diffOn=1 diffMix=50 diffDecay=80 lfo1Rate=0.06" \
  "0:filterCutoff:0.15 7:feedback:0.3 8:size:0.3"

preset "Night Field" "Ambient" \
  "mix=50 size=260 density=20 spray=1000 position=1200 reverse=40 panSpread=95 pitchRandom=0.1 octaveDown=20 diffOn=1 diffMix=45 diffDecay=72 rand1Rate=0.3 rand1Smooth=90" \
  "4:position:0.2 7:reverse:0.5 8:octaveDown:0.4"

preset "Fifths Above" "Ambient" \
  "mix=50 size=400 density=18 spray=450 position=300 voice1Prob=60 voice2Prob=60 voice3Prob=25 panSpread=85 feedback=28 diffOn=1 diffMix=50 diffDecay=74 filterOn=1 filterCutoff=7000" \
  "7:voice3Prob:0.5 8:feedback:0.3 0:position:0.1"

preset "Soft Static" "Ambient" \
  "mix=40 size=20 density=90 spray=300 position=150 jitter=100 panSpread=100 filterOn=1 filterType=2 filterCutoff=3000 filterReso=30 diffOn=1 diffMix=35" \
  "3:filterCutoff:0.2 7:density:0.3 8:spray:0.4"

# ---------------------------------------------------------------------------------------------
# Glitch

preset "Stutter Gate" "Glitch" \
  "mix=70 triggerMode=2 patternRate=5 size=55 positionSync=1 positionDiv=5 spray=0 jitter=0 windowShape=2 panSpread=20 step2Ratchet=2 step5Gate=0 step6Ratchet=4 step9Gate=0 step10Ratchet=2 step13Ratchet=3 step14Gate=0" \
  "7:size:0.3 8:reverse:0.6"

preset "Triplet Chop" "Glitch" \
  "mix=60 triggerMode=1 densityDiv=6 sizeSync=1 sizeDiv=3 positionSync=1 positionDiv=9 windowShape=2 spray=0 jitter=0 reverse=20 panSpread=50" \
  "4:reverse:0.3 7:pitchRandom:0.3 8:panSpread:0.4"

preset "Bit Scatter" "Glitch" \
  "mix=55 triggerMode=2 patternRate=2 size=25 spray=800 position=400 pitchRandom=12 scale=9 windowShape=3 panSpread=100 satOn=1 satType=1 satDrive=10 step2Prob=50 step4Prob=40 step6Prob=70 step8Prob=30 step10Prob=60 step12Prob=45 step14Prob=55 step16Prob=35" \
  "7:pitchRandom:0.4 8:satDrive:0.4"

preset "Ratchet Ladder" "Glitch" \
  "mix=60 triggerMode=2 patternRate=8 patternLength=8 size=40 positionSync=1 positionDiv=8 spray=0 jitter=0 windowShape=3 panSpread=40 octaveUp=25 step2Ratchet=2 step3Ratchet=3 step4Ratchet=4 step6Ratchet=2 step7Ratchet=3 step8Ratchet=4" \
  "7:octaveUp:0.5 8:size:0.3"

preset "Tape Stop Flick" "Glitch" \
  "mix=65 triggerMode=2 patternRate=5 size=110 positionSync=1 positionDiv=8 spray=0 windowShape=1 lfo1Shape=2 lfo1Sync=1 lfo1Div=14 lfo1Retrig=1 step4Gate=0 step8Ratchet=2 step12Gate=0 step16Ratchet=4" \
  "0:pitch:-0.25 7:reverse:0.5 8:feedback:0.4"

preset "Broken Loop" "Glitch" \
  "mix=60 triggerMode=1 densityDiv=5 size=90 positionSync=1 positionDiv=11 spray=0 jitter=20 reverse=30 rand1Sync=1 rand1Div=8 rand1Smooth=0" \
  "4:position:0.12 5:reverse:0.5 7:feedback:0.5 8:pitchRandom:0.3"

preset "Random Access" "Glitch" \
  "mix=60 triggerMode=1 densityDiv=5 size=80 spray=2000 position=2000 jitter=0 windowShape=2 reverse=50 panSpread=90 octaveUp=20 octaveDown=20" \
  "7:octaveUp:0.3 8:size:0.4"

preset "Sharp Edges" "Glitch" \
  "mix=60 triggerMode=2 patternRate=5 size=30 positionSync=1 positionDiv=5 windowShape=3 windowSkew=-80 spray=0 jitter=0 filterOn=1 filterType=1 filterCutoff=1200 filterReso=40 satOn=1 satType=1 satDrive=12 step2Gate=0 step4Ratchet=2 step6Gate=0 step7Ratchet=3 step10Gate=0 step12Ratchet=2 step15Ratchet=4" \
  "2:filterCutoff:0.25 7:filterCutoff:0.4 8:satDrive:0.3"

preset "Half Time Jump" "Glitch" \
  "mix=60 triggerMode=1 densityDiv=8 sizeSync=1 sizeDiv=8 positionSync=1 positionDiv=14 pitch=-12 spray=0 jitter=0 windowShape=1 panSpread=30 reverse=15" \
  "7:reverse:0.6 8:feedback:0.4"

# ---------------------------------------------------------------------------------------------
# Shimmer

preset "Fifth Halo" "Shimmer" \
  "mix=45 pitch=7 feedback=58 size=240 density=28 spray=160 position=200 panSpread=75 duckAmount=20 diffOn=1 diffPlace=0 diffMix=60 diffDecay=72 filterOn=1 filterPlace=0 filterCutoff=6500" \
  "7:feedback:0.25 8:filterCutoff:-0.4 0:position:0.1"

preset "Double Octave Air" "Shimmer" \
  "mix=40 voice1Interval=12 voice2Interval=24 voice2Prob=45 feedback=50 size=180 density=32 spray=220 position=220 panSpread=90 diffOn=1 diffPlace=0 diffMix=65 diffDecay=78 filterOn=1 filterPlace=0 filterCutoff=9000" \
  "7:voice2Prob:0.4 8:feedback:0.3"

preset "Crystal Rise" "Shimmer" \
  "mix=45 octaveUp=60 scale=9 pitchRandom=12 feedback=45 size=140 density=36 spray=300 position=260 panSpread=100 diffOn=1 diffMix=55 diffDecay=76 filterOn=1 filterType=1 filterCutoff=500" \
  "7:octaveUp:0.3 8:diffMix:0.3 4:panSpread:-0.2"

preset "Choir Of Glass" "Shimmer" \
  "mix=50 voice1Interval=12 voice2Interval=19 voice2Prob=50 voice3Interval=7 voice3Prob=40 size=420 density=20 spray=350 position=350 feedback=40 panSpread=85 diffOn=1 diffMix=60 diffDecay=82 diffSize=80" \
  "7:feedback:0.3 8:voice3Prob:0.4 0:fine:0.04"

preset "Dark Shimmer" "Shimmer" \
  "mix=45 pitch=12 octaveDown=35 feedback=60 size=300 density=24 spray=200 position=240 diffOn=1 diffPlace=0 diffMix=60 diffDecay=80 diffDamp=65 filterOn=1 filterPlace=0 filterCutoff=3000" \
  "7:filterCutoff:0.4 8:feedback:0.2"

preset "Slow Bloom" "Shimmer" \
  "mix=50 pitch=12 size=800 density=7 windowSkew=60 spray=300 position=500 feedback=50 panSpread=80 diffOn=1 diffPlace=0 diffMix=65 diffDecay=85 diffSize=85" \
  "7:size:0.2 8:diffDecay:0.15 0:position:0.1"

preset "Minor Halo" "Shimmer" \
  "mix=45 scale=3 root=9 voice1Interval=12 voice2Interval=15 voice2Prob=40 voice3Interval=19 voice3Prob=40 feedback=45 size=260 density=26 spray=200 position=220 panSpread=85 diffOn=1 diffMix=55 diffDecay=78" \
  "7:voice2Prob:0.4 8:feedback:0.3"

preset "Bright Tail" "Shimmer" \
  "mix=40 pitch=12 feedback=68 size=90 density=40 spray=100 position=120 windowShape=3 panSpread=100 duckAmount=40 diffOn=1 diffPlace=0 diffMix=50 diffDecay=70 filterOn=1 filterPlace=0 filterType=1 filterCutoff=700" \
  "7:feedback:0.2 8:duckAmount:-0.4 6:diffMix:0.2"

# ---------------------------------------------------------------------------------------------
# Frozen

preset "Still Lake" "Frozen" \
  "mix=85 freeze=1 size=900 density=10 spray=800 position=800 panSpread=85 jitter=60 diffOn=1 diffMix=45 diffDecay=80 lfo1Rate=0.04" \
  "0:position:0.15 7:pitch:0.25 8:diffMix:0.4"

preset "Ice Choir" "Frozen" \
  "mix=80 freeze=1 size=500 density=20 spray=1200 position=1000 voice2Prob=40 voice3Prob=40 panSpread=100 diffOn=1 diffMix=55 diffDecay=85" \
  "7:voice3Prob:0.4 8:spray:0.3 1:size:0.15"

preset "Slow Scan" "Frozen" \
  "mix=85 freeze=1 size=300 density=24 spray=40 position=2000 scan=0.25 panSpread=60 jitter=40 diffOn=1 diffMix=30" \
  "7:scan:0.4 8:size:0.3"

preset "Frozen Pulse" "Frozen" \
  "outputGain=4 mix=80 freeze=1 triggerMode=1 densityDiv=8 size=200 spray=600 position=700 windowShape=3 panSpread=70 jitter=0 diffOn=1 diffMix=35 diffDecay=60" \
  "7:spray:0.4 8:pitchRandom:0.3"

preset "Suspended Chord" "Frozen" \
  "mix=85 freeze=1 size=1200 density=6 spray=300 position=500 voice2Prob=70 voice3Prob=50 voice4Prob=50 panSpread=90 diffOn=1 diffMix=50 diffDecay=88 diffSize=85" \
  "7:voice4Prob:0.4 8:diffDecay:0.1 0:position:0.05"

preset "Glacier" "Frozen" \
  "mix=85 freeze=1 pitch=-12 size=1500 density=5 spray=1000 position=1200 scan=0.05 panSpread=80 filterOn=1 filterCutoff=2500 diffOn=1 diffMix=55 diffDecay=90" \
  "7:filterCutoff:0.4 8:scan:0.2 3:position:0.2"

preset "Shiver" "Frozen" \
  "outputGain=-3 mix=80 freeze=1 size=40 density=70 spray=30 position=400 jitter=90 panSpread=70 scan=0.5 lfo3Rate=5" \
  "2:position:0.01 7:scan:0.3 8:size:0.4 0:panSpread:0.2"

preset "Reverse Hold" "Frozen" \
  "mix=85 freeze=1 reverse=100 size=700 density=9 windowSkew=60 spray=900 position=900 panSpread=85 diffOn=1 diffMix=45 diffDecay=75" \
  "7:reverse:-0.5 8:size:0.3 0:position:0.15"

# ---------------------------------------------------------------------------------------------
# Delay

preset "Dotted Eighth" "Delay" \
  "mix=40 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=10 sizeSync=1 sizeDiv=10 feedback=50 spray=0 jitter=0 windowShape=1 panSpread=30 filterOn=1 filterPlace=0 filterCutoff=5000" \
  "7:feedback:0.4 8:filterCutoff:0.3"

preset "Scattered Echo" "Delay" \
  "mix=45 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=11 sizeSync=1 sizeDiv=8 feedback=50 spray=0 jitter=0 panSpread=100 windowShape=1" \
  "7:feedback:0.4 8:panSpread:-0.6"

preset "Rising Repeats" "Delay" \
  "mix=40 triggerMode=1 densityDiv=11 positionSync=1 positionDiv=11 sizeSync=1 sizeDiv=11 pitch=5 feedback=60 spray=0 jitter=0 windowShape=1 filterOn=1 filterPlace=0 filterCutoff=6000" \
  "7:feedback:0.3 8:pitch:0.15"

preset "Falling Repeats" "Delay" \
  "mix=40 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=10 sizeSync=1 sizeDiv=8 pitch=-5 feedback=60 spray=0 jitter=0 windowShape=1 filterOn=1 filterPlace=0 filterCutoff=3500" \
  "7:feedback:0.3 8:pitch:-0.15"

preset "Slapback Cloud" "Delay" \
  "mix=35 size=60 density=40 position=90 spray=25 jitter=50 panSpread=80 feedback=20" \
  "7:position:0.05 8:feedback:0.5"

preset "Dub Chamber" "Delay" \
  "mix=45 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=10 sizeSync=1 sizeDiv=8 feedback=70 spray=0 jitter=0 windowShape=1 filterOn=1 filterPlace=0 filterType=2 filterCutoff=1200 filterReso=25 satOn=1 satPlace=0 satDrive=8 diffOn=1 diffMix=25 diffDecay=55" \
  "7:feedback:0.25 8:filterCutoff:0.3 0:filterCutoff:0.08"

preset "Reverse Echo" "Delay" \
  "mix=45 triggerMode=1 densityDiv=11 positionSync=1 positionDiv=11 sizeSync=1 sizeDiv=11 reverse=100 feedback=40 spray=0 jitter=0 panSpread=40" \
  "7:feedback:0.4 8:reverse:-1"

preset "Long Smeared Echo" "Delay" \
  "mix=45 size=300 density=12 position=750 spray=120 jitter=40 feedback=55 panSpread=70 filterOn=1 filterPlace=0 filterCutoff=4000 diffOn=1 diffPlace=0 diffMix=30 diffSize=30 diffDecay=30" \
  "7:spray:0.3 8:feedback:0.3"

# ---------------------------------------------------------------------------------------------
# Chorus

preset "Slow Ensemble" "Chorus" \
  "mix=50 size=80 density=40 position=30 spray=15 pitchRandom=0.08 panSpread=100 width=130 jitter=70 lfo1Rate=0.15 lfo2Rate=0.23" \
  "0:fine:0.06 1:position:0.015 7:pitchRandom:0.1 8:width:0.25"

preset "Detuned Pair" "Chorus" \
  "mix=50 size=100 density=30 position=20 spray=5 pitchRandom=0.2 fine=8 panSpread=100 jitter=50" \
  "7:pitchRandom:0.15 8:position:0.03"

preset "Octave Doubler" "Chorus" \
  "mix=45 size=90 density=35 position=25 spray=8 voice1Prob=50 voice3Prob=30 voice4Prob=30 panSpread=70 jitter=60" \
  "7:voice3Prob:0.5 8:voice4Prob:0.5"

preset "Vibrato Cloud" "Chorus" \
  "mix=100 size=70 density=45 position=20 spray=4 panSpread=40 jitter=40 lfo1Rate=5.5" \
  "0:fine:0.15 7:lfo1Rate:0.2 8:panSpread:0.5"

preset "Rotary Motion" "Chorus" \
  "mix=70 size=60 density=50 position=20 spray=6 panSpread=60 jitter=50 lfo1Rate=0.8 lfo2Rate=6" \
  "0:panSpread:0.4 1:fine:0.06 1:outputGain:0.04 7:lfo2Rate:-0.3 8:width:0.3"

preset "Wide Unison" "Chorus" \
  "outputGain=-3 mix=55 size=150 density=28 position=40 spray=20 pitchRandom=0.18 panSpread=100 width=160 jitter=90" \
  "7:width:-0.4 8:pitchRandom:0.15"

# ---------------------------------------------------------------------------------------------
# Reverse

preset "Rewind" "Reverse" \
  "mix=70 reverse=100 size=400 density=10 position=450 spray=30 jitter=20 panSpread=30" \
  "7:size:0.3 8:feedback:0.4"

preset "Reverse Pluck" "Reverse" \
  "mix=55 triggerMode=1 densityDiv=8 reverse=100 sizeSync=1 sizeDiv=8 positionSync=1 positionDiv=8 windowShape=3 windowSkew=80 spray=0 jitter=0 panSpread=50" \
  "7:feedback:0.4 8:octaveUp:0.4"

preset "Ghost Reply" "Reverse" \
  "mix=45 reverse=100 size=600 density=5 position=700 spray=200 duckAmount=60 duckRelease=500 panSpread=70 diffOn=1 diffMix=45 diffDecay=65" \
  "7:duckAmount:-0.4 8:diffMix:0.4"

preset "Mirror Hall" "Reverse" \
  "mix=50 reverse=50 size=350 density=16 position=400 spray=300 panSpread=90 feedback=25 diffOn=1 diffMix=55 diffDecay=75" \
  "7:reverse:0.5 8:diffDecay:0.2"

preset "Undertow" "Reverse" \
  "mix=60 reverse=100 pitch=-12 size=1000 density=4 windowSkew=50 position=1100 spray=200 panSpread=60 filterOn=1 filterCutoff=3000 diffOn=1 diffMix=35" \
  "7:filterCutoff:0.4 8:size:0.25"

preset "Reverse Shimmer" "Reverse" \
  "mix=50 reverse=100 pitch=12 size=500 density=12 windowSkew=60 position=550 spray=150 feedback=45 panSpread=85 diffOn=1 diffPlace=0 diffMix=55 diffDecay=78" \
  "7:feedback:0.3 8:diffMix:0.3"

# ---------------------------------------------------------------------------------------------
# Cascades

preset "Major Steps" "Cascades" \
  "mix=55 triggerMode=2 patternRate=5 scale=2 pitchRandom=12 size=140 position=200 feedback=30 windowShape=3 panSpread=70 diffOn=1 diffMix=25 step3Prob=70 step7Prob=60 step11Prob=65 step14Ratchet=2" \
  "7:pitchRandom:0.4 8:feedback:0.4"

preset "Falling Leaves" "Cascades" \
  "mix=50 triggerMode=1 densityDiv=8 scale=5 octaveDown=40 pitchRandom=8 size=220 position=300 spray=200 feedback=40 windowShape=3 panSpread=90 diffOn=1 diffMix=35 diffDecay=65" \
  "7:octaveDown:0.4 8:spray:0.3"

preset "Harp Run" "Cascades" \
  "mix=50 triggerMode=1 densityDiv=3 scale=4 pitchRandom=16 size=120 position=150 windowShape=3 panSpread=80 octaveUp=20 jitter=0 diffOn=1 diffMix=30" \
  "7:pitchRandom:0.3 8:diffMix:0.4 6:octaveUp:0.3"

preset "Whole Tone Spiral" "Cascades" \
  "mix=50 triggerMode=1 densityDiv=6 scale=8 pitchRandom=10 size=180 position=250 feedback=45 panSpread=85 lfo1Rate=0.1" \
  "0:pitch:0.25 7:feedback:0.3 8:pitchRandom:0.4"

preset "Dorian Rain" "Cascades" \
  "mix=50 scale=6 root=2 density=12 jitter=100 pitchRandom=14 size=160 position=400 spray=600 windowShape=3 panSpread=100 octaveUp=25 diffOn=1 diffMix=40 diffDecay=70" \
  "7:density:0.3 8:octaveUp:0.4"

preset "Octave Ladder" "Cascades" \
  "mix=55 triggerMode=2 patternRate=8 patternLength=8 scale=9 pitchRandom=20 size=200 position=250 feedback=35 windowShape=3 panSpread=60 step2Ratchet=2 step6Ratchet=2 step8Ratchet=3" \
  "7:feedback:0.4 8:size:0.3"

preset "Phrygian Echoes" "Cascades" \
  "mix=50 triggerMode=1 densityDiv=10 scale=7 root=4 pitchRandom=9 size=260 positionSync=1 positionDiv=10 feedback=50 windowShape=1 panSpread=75 filterOn=1 filterPlace=0 filterCutoff=5000" \
  "7:feedback:0.3 8:pitchRandom:0.4"

# ---------------------------------------------------------------------------------------------
# Lo-Fi

preset "Cassette Wobble" "Lo-Fi" \
  "mix=100 size=140 density=30 position=50 spray=6 panSpread=5 width=70 jitter=30 lfo1Rate=0.4 lfo2Rate=3.1 satOn=1 satDrive=10 filterOn=1 filterCutoff=5000" \
  "0:fine:0.1 1:fine:0.03 7:satDrive:0.3 8:filterCutoff:-0.3"

preset "Radio Band" "Lo-Fi" \
  "outputGain=6 mix=100 size=60 density=50 position=30 spray=4 panSpread=0 width=30 filterOn=1 filterType=2 filterCutoff=1500 filterReso=35 satOn=1 satType=1 satDrive=12" \
  "7:filterCutoff:0.3 8:satDrive:0.3"

preset "Crushed Grains" "Lo-Fi" \
  "mix=70 size=8 density=120 position=20 spray=2 jitter=0 windowShape=2 panSpread=20 satOn=1 satType=1 satDrive=18 filterOn=1 filterCutoff=4000" \
  "7:size:0.2 8:density:-0.3"

preset "Dropouts" "Lo-Fi" \
  "mix=100 size=100 density=30 position=40 spray=5 panSpread=10 rand1Rate=3 rand1Smooth=10 filterOn=1 filterCutoff=4500 satOn=1 satDrive=8" \
  "4:density:-0.35 5:fine:0.04 7:density:-0.3 8:filterCutoff:-0.3"

preset "Old Vinyl" "Lo-Fi" \
  "outputGain=9 mix=100 size=180 density=24 position=60 spray=10 panSpread=15 width=60 pitchRandom=0.05 lfo1Rate=0.55 filterOn=1 filterType=2 filterCutoff=1800 filterReso=10 satOn=1 satDrive=7" \
  "0:fine:0.05 7:pitchRandom:0.1 8:width:0.4"

preset "Warped Memory" "Lo-Fi" \
  "mix=80 size=350 density=12 position=300 spray=150 pitchRandom=0.25 reverse=15 panSpread=50 lfo1Shape=4 lfo1Rate=0.3 satOn=1 satDrive=9 filterOn=1 filterCutoff=3500 diffOn=1 diffMix=20" \
  "0:fine:0.25 7:reverse:0.4 8:diffMix:0.4"

preset "Telephone" "Lo-Fi" \
  "outputGain=3 mix=100 size=50 density=60 position=25 spray=3 panSpread=0 width=0 filterOn=1 filterType=2 filterCutoff=1100 filterReso=45 satOn=1 satType=1 satDrive=16" \
  "7:filterCutoff:0.25 8:width:0.5"

# ---------------------------------------------------------------------------------------------
# Ducked

preset "Shadow Pad" "Ducked" \
  "mix=50 duckAmount=90 duckAttack=10 duckRelease=600 size=700 density=10 spray=700 position=500 feedback=45 panSpread=90 diffOn=1 diffMix=60 diffDecay=80" \
  "7:duckAmount:-0.5 8:feedback:0.3"

preset "Breathing Shimmer" "Ducked" \
  "mix=45 duckAmount=80 duckRelease=350 pitch=12 feedback=55 size=250 density=26 spray=200 position=220 diffOn=1 diffPlace=0 diffMix=60 diffDecay=75" \
  "7:duckAmount:-0.5 8:diffMix:0.3"

preset "Tail Only" "Ducked" \
  "mix=50 duckAmount=100 duckAttack=2 duckRelease=250 size=200 density=30 spray=400 position=300 feedback=50 panSpread=80 diffOn=1 diffMix=45" \
  "7:duckAmount:-0.5 8:spray:0.3"

preset "Pumping Echo" "Ducked" \
  "mix=50 triggerMode=1 densityDiv=8 positionSync=1 positionDiv=10 sizeSync=1 sizeDiv=8 feedback=55 duckAmount=75 duckAttack=3 duckRelease=180 spray=0 jitter=0 windowShape=1" \
  "7:duckAmount:-0.5 8:feedback:0.3"

preset "Answering Cloud" "Ducked" \
  "mix=50 duckAmount=85 duckRelease=800 size=120 density=40 spray=900 position=600 octaveUp=30 reverse=30 panSpread=100 diffOn=1 diffMix=40" \
  "7:duckAmount:-0.5 8:octaveUp:0.4"

# ---------------------------------------------------------------------------------------------
# Smear

preset "Soft Focus" "Smear" \
  "mix=55 size=140 density=70 position=50 spray=110 jitter=100 panSpread=50" \
  "7:spray:0.3 8:size:0.3"

preset "Time Blur" "Smear" \
  "mix=70 size=250 density=50 position=150 spray=300 jitter=100 panSpread=70 diffOn=1 diffSize=30 diffDecay=30 diffMix=35" \
  "7:spray:0.3 8:position:0.15"

preset "Pad Maker" "Smear" \
  "mix=85 size=600 density=30 position=350 spray=600 jitter=100 panSpread=80 filterOn=1 filterCutoff=5000 diffOn=1 diffMix=40 diffDecay=60" \
  "7:filterCutoff:0.3 8:size:0.25"

preset "Attack Eraser" "Smear" \
  "mix=100 size=180 density=60 position=100 spray=160 jitter=100 panSpread=40" \
  "7:spray:0.3 6:density:-0.2 8:panSpread:0.5"

preset "Spectral Wash" "Smear" \
  "mix=65 size=30 density=200 position=60 spray=250 jitter=100 panSpread=100 diffOn=1 diffMix=50 diffDecay=55 diffSize=40" \
  "7:size:0.2 8:diffMix:0.3"

preset "Thick Air" "Smear" \
  "mix=60 size=320 density=45 position=200 spray=200 jitter=100 pitchRandom=0.1 panSpread=90 width=140" \
  "7:pitchRandom:0.15 8:spray:0.3"

preset "Reverse Blur" "Smear" \
  "mix=65 reverse=50 size=160 density=60 position=180 spray=140 jitter=100 panSpread=70 diffOn=1 diffMix=30 diffSize=25 diffDecay=25" \
  "7:reverse:0.5 8:size:0.3"

# ---------------------------------------------------------------------------------------------
# Drones

preset "Deep Bed" "Drones" \
  "mix=85 pitch=-24 size=1800 density=6 position=400 spray=400 feedback=70 panSpread=60 filterOn=1 filterPlace=0 filterCutoff=1500 diffOn=1 diffPlace=0 diffMix=50 diffDecay=90" \
  "7:feedback:0.2 8:filterCutoff:0.3"

preset "Fifth Drone" "Drones" \
  "outputGain=-6 mix=85 size=1500 density=8 position=300 spray=300 voice2Prob=80 voice4Prob=60 feedback=55 panSpread=80 diffOn=1 diffPlace=0 diffMix=50 diffDecay=88 filterOn=1 filterPlace=0 filterCutoff=3000 lfo1Rate=0.05" \
  "0:filterCutoff:0.15 7:voice2Prob:-0.5 8:feedback:0.2"

preset "Organ Pipe" "Drones" \
  "mix=85 size=1000 density=12 position=250 spray=100 voice3Prob=70 voice4Prob=70 feedback=65 panSpread=40 jitter=30 filterOn=1 filterPlace=0 filterCutoff=4000" \
  "7:voice3Prob:-0.4 8:filterCutoff:0.3"

preset "Infinite Sustain" "Drones" \
  "outputGain=-6 mix=80 size=700 density=8 position=150 spray=120 feedback=66 panSpread=70 diffOn=1 diffPlace=0 diffMix=55 diffDecay=92 diffSize=85 filterOn=1 filterPlace=0 filterCutoff=3500" \
  "7:feedback:0.12 8:filterCutoff:0.3"

preset "Machine Hum" "Drones" \
  "outputGain=3 mix=85 pitch=-12 size=60 density=80 position=100 spray=10 jitter=5 feedback=70 panSpread=30 filterOn=1 filterPlace=0 filterType=2 filterCutoff=400 filterReso=40 satOn=1 satPlace=0 satDrive=10" \
  "7:filterCutoff:0.3 8:size:0.3"

preset "Evolving Bed" "Drones" \
  "outputGain=-3 mix=85 size=1200 density=9 position=600 spray=800 feedback=68 pitchRandom=0.1 panSpread=90 lfo1Rate=0.03 lfo2Rate=0.05 lfo4Rate=0.02 diffOn=1 diffPlace=0 diffMix=50 diffDecay=88 filterOn=1 filterPlace=0 filterCutoff=2800" \
  "0:position:0.2 1:filterCutoff:0.2 3:size:0.2 7:feedback:0.2 8:pitch:0.25"

preset "Octave Stack" "Drones" \
  "outputGain=-9 mix=85 size=1400 density=8 position=350 spray=250 voice1Prob=80 voice3Prob=60 voice4Prob=80 octaveDown=30 feedback=62 panSpread=75 diffOn=1 diffPlace=0 diffMix=50 diffDecay=88 filterOn=1 filterPlace=0 filterCutoff=2600" \
  "7:octaveDown:0.4 8:voice3Prob:0.4"

preset "Singing Bowl" "Drones" \
  "outputGain=-6 mix=80 size=900 density=10 position=200 spray=150 feedback=58 pitchRandom=0.04 panSpread=70 filterOn=1 filterPlace=0 filterType=2 filterCutoff=900 filterReso=40 diffOn=1 diffPlace=0 diffMix=45 diffDecay=85 lfo1Rate=0.07" \
  "0:filterCutoff:0.1 7:filterReso:0.3 8:feedback:0.15"

echo "Wrote $(ls "$out"/*.xml | wc -l) presets to $out"
