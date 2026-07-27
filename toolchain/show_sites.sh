#!/bin/bash
cd /c/Users/dalla/Vulture/vulture5-win/win/vulture || exit 2
for L in 339 398 415 446 465 487 609 790 884; do
  echo "----- line $L -----"
  sed -n "$((L-5)),$((L+8))p" vulture_main.cpp
done
