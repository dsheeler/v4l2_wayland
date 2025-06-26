makev4l2 /dev/video2 3840 2160 true
makev4l2 /dev/video1 1920 1080 true
makev4l2 /dev/video6 1920 1080 true
makev4l2 /dev/video8 1024 524 true
sleep 8
rotate v4l23 270 3;
mv v4l23 2800 524 4;
mv v4l21 0 0 4;
mv v4l22 0 1080 4;
