#ifndef IKURA_RUNTIME_LOADER_H
#define IKURA_RUNTIME_LOADER_H

#include "common/stream.h"
#include "ikura/script/context.h"

namespace Ikura {

// Opens the shared "isf" cabinet and loads its entry-point script. The
// reference always boots a game by loading a script named "TITLE"
// (VileVN reference: IkuraDecoder::EventGameDialog(VD_TITLE),
// src/ikura/ikuradecoder.cpp - LoadScript("TITLE", "ISF")).
//
// Takes ownership of cabinetStream either way (matches
// Format::Archive::Cabinet::open). Returns nullptr if the cabinet can't be
// parsed or has no TITLE.ISF entry.
VM::Context *loadTitleScript(Common::SeekableReadStream *cabinetStream);

} // End of namespace Ikura

#endif
