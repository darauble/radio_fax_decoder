// fax library: everything (radio fax decoding from WAV recordings or live audio)
//
//   fax::decode_file / fax::find_clock_correction    recordings, high level   (FileDecoder.h)
//   fax::Decoder                                     streaming, push audio    (Decoder.h)
//   fax::SlantEstimator                              clock error from slant   (SlantEstimator.h)
//   fax::WavReader, fax::PgmWriter                   file formats
#pragma once

#include "fax/Version.h"
#include "fax/Log.h"
#include "fax/Options.h"
#include "fax/Wav.h"
#include "fax/PgmWriter.h"
#include "fax/SlantEstimator.h"
#include "fax/Decoder.h"
#include "fax/FileDecoder.h"
