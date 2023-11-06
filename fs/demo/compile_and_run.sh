#!/bin/bash

mkdir build

cd build || exit

cmake ..

make

./demo

cd - || exit