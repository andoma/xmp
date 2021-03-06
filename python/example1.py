#!/usr/bin/python
"""
Example 1: A simple modplayer in python using xmp and pyaudio
"""

import sys
import os
import pyaudio
import pyxmp

class Sound:
    """ Sound output manager

    This class uses PyAudio to play sound.

    """
    def __init__(self):
        self._audio = pyaudio.PyAudio()
        self._stream = self._audio.open(format=pyaudio.paInt16, channels=2,
	                                rate=44100, output=True)

    def write(self, buf):
        """Write to PyAudio sound stream."""
        self._stream.write(buf)

    def close(self):
        """Close stream and free resources."""
        self._stream.close()
        self._audio.terminate()


def show_info(mod):
    """
    Display module information.
    """
    print "Name: %s" % (mod.name)
    print "Type: %s" % (mod.type)
    print "Instruments: %d   Samples: %d" % (mod.ins, mod.smp)
    for i in range (mod.ins):
        ins = mod.xxi[i]
        if len(ins.name.rstrip()) > 0:
            print(" %2d %-32.32s  " % (i, mod.xxi[i].name))
    
def play(filename):
    """
    Our mod player.
    """
    finfo = pyxmp.struct_xmp_frame_info()
    minfo = pyxmp.struct_xmp_module_info()
    
    xmp = pyxmp.Xmp()
    try:
        xmp.load_module(filename)
    except IOError, error:
        sys.stderr.write('{0}: {1}\n'.format(filename, error.strerror))
        sys.exit(1)
    
    sound = Sound()
    
    xmp.start_player(44100, 0)
    xmp.get_module_info(minfo)
    
    mymod = minfo.mod[0]
    show_info(mymod)
    
    while xmp.play_frame():
        xmp.get_frame_info(finfo)
        if finfo.loop_count > 0:
            break
    
        if finfo.frame == 0:
            sys.stdout.write(" %3d/%3d  %3d/%3d\r" %
                (finfo.pos, mymod.len, finfo.row, finfo.num_rows))
            sys.stdout.flush()
    
        sound_buffer = xmp.get_buffer(finfo)
        sound.write(sound_buffer)
    
    xmp.end_player()
    sound.close()
    xmp.release_module()


if len(sys.argv) < 2:
    print "Usage: %s <module>" % (os.path.basename(sys.argv[0]))
    sys.exit(1)

play(sys.argv[1])
