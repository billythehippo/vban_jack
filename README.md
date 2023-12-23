# vban_jack

Here is a small set of VBAN Jack utilities for GNU/Linux-based OSes.
The main difference from Benoit's version is receptors' algorythm:
1. packet reception is realised in separate thread,
2. ring-buffer read/write is per-frame in different threads.
This is to prevent latency-increasing and mask network XRUNs.
We (rock-bands I play drums with) already use them in on-stage mode.

vban_jack_emitter and vban_jack_receptor are CLI versions.
To build them here is 2 ways:

make
QTCreator

qvban_jack_emitter and qvban_jack_receptor are QT-GUI versions.
Use QT and QTCreator to build them.

USAGE CLI: run vban_jack_emitter or vban_jack_receptor with key -h to see help
(they are principally like Benoit's utils),
USAGE GUI - just run them, they are intuitive.

Also you can build these utils for Mac OS (some changes in udpsocket.cpp are required)

Std messages per 1/2 sec are for async monitoring.

Main dependency is libjack2-dev (name for Debian-based OS's).
They can also be used with pipewire (pipewire-jack packet is required).
