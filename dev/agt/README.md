# Atari Game Tools #

         .__________________________________________________.
         ||////////////////////////////////////////////////||
         ||////////////////////////////////////////////////||
         ||////////////////////////////////////////////////||
         ||////////////////////////////////////////////////||    ___.
         ||////////////////////////////////////////////////||   /    \
         !__________________________________________________!  |      |
         |   __ __ __ __ __ __ __ __ __ __  /|\ ATARI 1040ST|  |      |
         |__/_//_//_//_//_//_//_//_//_//_/____________--____|  |  .---|---.
         | ______________________________________________   |  |  |   |   |
         | [][][][][][][][][][][][][][][__] [_][_] [][][][] |  |  |---'---|
         | [_][][][][][][][][][][][][]| |[] [][][] [][][][] |  |  |       |
         | [__][][][][][][][][][][][][__|[] [][][] [][][][] |  |  |       |
         | [_][][][][][][][][][][][][_]            [][][]|| |  |  |  /|\  |
         |    [_][________________][_]             [__][]LI |  |   \_____/
         |__________________________________________________|  ;
                                                          \___/



### What is this repository for? ###

* Tools and code for rapid prototyping of Atari STE games
* Game framework samples

### How do I get set up? ###

* To set up a build environment you'll need Atari MiNT-GCC 4.6.4 (or one of the other GCC compilers now available), GNU Make, GIT and a way to test your Atari code. See [this page](https://bitbucket.org/d_m_l/agtools/wiki/Host%20Development%20Environments) for details on different host platforms.
* The essential AGT tools are already provided in the project /bin directory. These should work on Windows, MacOS & RPI-64 with a [suitably configured environment](https://bitbucket.org/d_m_l/agtools/wiki/Host%20Development%20Environments)
* These tools are provided as source in the repo and can be built with GCC on most other host systems.
* The project is Make-driven which requires a specific environment on Windows: Cygwin, MinGW or WSL (typically has been Cygwin since a prebuilt GCC 4.6.4 package is available for it)
* On all systems, the GCC compiler tools should be configured on the default $PATH to work - so test that first!
* An emulator will help (Hatari, STEEM, SainT) but nothing beats real hardware!
* *   [Hatari - Windows nightly builds](http://antarctica.no/~hatari/)
* *   [STEEM Engine](http://steem.atari.st/)
* *   STEEM SSE
* *   STEEM Boiler

### Host environments tested ###

* Windows: Cygwin x86/x64, Ubuntu on WSL
* MacOS x64 (M1 should work under Rosetta2 or tools can be built locally for M1)
* Linux x64
* RaspberryPI-OS 64 (Linux/aarch64)

### Debugging tools ###

Code can be debugged natively on the Atari with MonST, Bugaboo, Db and other options but there are some very useful debugging capabilities in existing emulators.

* Hatari: has commandline debugger, execution history, profiler, exception filters, message print capture/logging (natfeats/xbios), programmable breakpoints and more.
* [STEEM BoilerRoom/SSE Debug](https://sourceforge.net/projects/steemsse/files/): has interactive debugger, execution history, exception filters, message print capture/logging (via register port), flexible breakpoints and more. [a great BoilerRoom guide here](http://beyondbrown.d-bug.me/post/steem-debug-the-boiler-room/#content)

Both are supported directly by AGT via build-time configuration.

### Remote testing ###

There are various options for getting your project onto real hardware for honest testing (and this is not an exhaustive list).

* Direct-connect options:
* * ultraDev cartridge (very good option!)
* * HxC floppy emulator (USB version: size-limited to floppy media, one at a time)
* * PARCP (note: generally requires disk storage at the ST side, PARCP is transfer-only)
* SDCard options:
* * UltraSatan (SDCard version: size-limited to floppy media, can select between images though)
* * HxC floppy emulator (SDCard version: size-limited to floppy media but multiple images per SDCard)

### Reference ###

* the AGT wiki can be found [here](https://bitbucket.org/d_m_l/agtools/wiki/Home)
* the RMAC assembler manual can be found [here](http://rmac.is-slick.com/)
