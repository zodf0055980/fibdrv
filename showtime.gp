reset
set ylabel 'time(ns)'
set xlabel 'size'
set key left top
set title 'runtime'
set term png enhanced font 'Verdana,10'

set output 'runtimeclient.png'

plot [0:100][0:1000]'time.txt' \
   using 1:2 with linespoints linewidth 2 title 'user', \
'' using 1:3 with linespoints linewidth 2 title 'kernel' , \