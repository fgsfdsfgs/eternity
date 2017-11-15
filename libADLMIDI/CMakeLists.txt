##-----------------------------------------------------------------------------
##
## Copyright (C) 2017 Max Waine
##
##-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 2.4)

project(libadlmidi)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)


##----------------------------------------------------------------------------|
## Environment Configuration                                                  |
##

include_directories (include)
include_directories (src)


##----------------------------------------------------------------------------|
## Targets                                                                    |
##

add_library (libadlmidi STATIC
   src/adldata.cpp
   src/adlmidi.cpp
   src/adlmidi_load.cpp
   src/adlmidi_midiplay.cpp
   src/adlmidi_mus2mid.c
   src/adlmidi_opl3.cpp
   src/adlmidi_private.cpp
   src/adlmidi_xmi2mid.c
   src/dbopl.cpp
   src/nukedopl3.c
)
