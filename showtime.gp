reset
set ylabel 'time(ns)'
set xlabel 'size'
set key left top
set title 'runtime'
set term png enhanced font 'Verdana,10'

set output 'runtimeclient.png'

plot [0:100][200:1000]'time.txt' \
    using 2:xtic(1) with linespoints linewidth 1 title 'user space'
