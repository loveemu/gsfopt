gsfopt
======

Gsf set optimizer / timer created by Caitsith2, revised by loveemu.
<https://github.com/loveemu/gsfopt>

All ripped sets should be optimized before release, to remove unused code/data unrelated to music playback.

This modified version uses vio2sf core, instead of old VisualBoyAdvance core.

Downloads
---------

- [Latest release](https://github.com/loveemu/gsfopt/releases/latest)
- [gsfopt 1.0 (Caitsith2's gsfopt + bugfix)](https://github.com/loveemu/gsfopt/releases/tag/v1.0-vba172)

Usage
-----

Syntax: gsfopt [options] [-s or -l or -f or -t] [gsf files]

### Options ###

`-T [time]`
  : Runs the emulation till no new data has been found for [time] specified.
    Time is specified in mm:ss.nnn format   
    mm = minutes, ss = seoconds, nnn = milliseconds

`-P [bytes]`
  : I am paranoid, and wish to assume that any data within [bytes] bytes of a used byte,
    is also used

#### File Processing Modes ####

`-f [gsf files]`
  : Optimize single files, and in the process, convert minigsfs/gsflibs to single gsf files

`-l [gsf files]`
  : Optimize the gsflib using passed gsf files.

`-r [gsf files]`
  : Convert to Rom files, no optimization

`-s [gsflib] [Hex offset] [Count]`
  : Optimize gsflib using a known offset/count

`-t [options] [gsf files]`
  : Times the GSF files. (for auto tagging, use the -T option)
    Unlike psf playback, silence detection is MANDATORY
    Do NOT try to evade this with an excessively long silence detect time.
    (The max time is less than 2*Verify loops for silence detection)

##### [options] for -t #####

`-V [time]`
  : Length of verify loops at end point. (Default 20 seconds)

`-L [count]`
  : Number of loops to time for. (Default 2, max 255)

`-T`
  : Tag the songs with found time.
    A Fade is also added if the song is not detected to be one shot.

`-F [time]`
  : Length of looping song fade. (default 10.000)

`-f [time]`
  : Length of one shot song fade. (default 1.000)

`-s [time]`
  : Time in seconds for silence detection (default 15 seconds)
    Max (2*Verify loop count) seconds.

