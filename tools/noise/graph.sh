#!/bin/bash

isolist="200 400 800 1600"

for iso in $isolist
do
  cat src/common/noiseprofiles.h | grep -E '{"' | grep "iso ${iso}\"" | tr -d "{}()\"" | cut -d '_' -f2 | cut -d ',' -f1 | tr " " "_" | sed -e "s/_iso_${iso}//" | nl > cams.txt 
  cat src/common/noiseprofiles.h | grep -E '{"' | grep "iso ${iso}\"" | tr -d "{}()\"" | cut -d '_' -f2 | cut -d ',' -f6 | nl > poissonian.txt
  cat src/common/noiseprofiles.h | grep -E '{"' | grep "iso ${iso}\"" | tr -d "{}()\"" | cut -d '_' -f2 | cut -d ',' -f9 | nl > gaussian.txt

  join cams.txt poissonian.txt > tmp.txt
  join tmp.txt gaussian.txt > noise_${iso}.dat

  rm -f cams.txt poissonian.txt gaussian.txt tmp.txt

  gnuplot << EOF
  set term pdf fontscale 0.5 size 10, 10
  set output 'noise_iso_$iso.pdf'
#  for web:
#  set term png fontscale 0.8 size 700,700 
#  set output 'noise_iso_$iso.png'
  set format x "%g"
  set format y "%g"
  set xlabel 'poissonian'
  set ylabel 'gaussian'
  plot "noise_${iso}.dat" u 3:4 w p title "iso $iso", '' u 3:4:(sprintf("\n%s", stringcolumn(2))) w labels notitle
EOF
done

rm -f noise_*.dat


