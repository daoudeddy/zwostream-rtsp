#!/usr/bin/env bash

set -e

echo "Starting cam..."

#echo 32 > /sys/module/usbcore/parameters/usbfs_memory_mb
mkdir -p ./data/capture ./data/upload ./data/incomplete

if [[ -n "$(ls -A /data/capture)" ]]; then
  echo "Moving existing files from capture to incomplete"
  mv ./data/capture/* ./data/incomplete/
fi

while [[ true ]]; do
  FILENAME=raw-$(date --utc +%Y%m%dT%H%M%S)Z.mp4
  echo "Starting capturing for 1h to /data/capture/$FILENAME"
  ./zwostream -G -d 1h -p RAW8 \
    | ffmpeg -hide_banner -loglevel warning -y \
    -f rawvideo -vcodec rawvideo \
    -video_size 1280x960 \
    -pixel_format gray8 \
    -use_wallclock_as_timestamps 1 \
    -i pipe:0 \
    -movflags +faststart \
    -c:v h264 -b:v 1024k -vsync vfr \
    -metadata creation_time="$(date --utc +%FT%T)Z" \
    ./data/capture/$FILENAME
  echo -e "\n\nMoving $FILENAME to upload folder"
  mv ./data/capture/$FILENAME ./data/upload/$FILENAME
done
