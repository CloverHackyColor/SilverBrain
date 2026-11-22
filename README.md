# SilverBrain
This is crossplatform graphical frontend for llama.cpp. Our fork https://github.com/CloverHackyColor/llama.cpp is used, but master branch is not good, use silver branch.

## Download
git clone --recurse-submodule https://github.com/CloverHackyColor/SilverBrain.git
cd third_party/llama.cpp
git checkout silver
git pull

## To compile
* cmake --preset macos && cmake --build build 
* cmake --preset windows && cmake --build build 

## Requirements
Qt6 preferable, Qt5.15 is posiible
