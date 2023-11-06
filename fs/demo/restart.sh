rm -rf mnt &&
dd if=./disk of=~/ddriver bs=512 count=8192 &&
mkdir mnt