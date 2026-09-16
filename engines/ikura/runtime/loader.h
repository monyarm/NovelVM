#ifndef IKURA_RUNTIME_LOADER_H
#define IKURA_RUNTIME_LOADER_H

#include "common/archive.h"
#include "ikura/script/context.h"

namespace Ikura {

// Loads the entry-point script out of an already-open "isf" cabinet. The
// reference always boots a game by loading a script named "TITLE"
// (VileVN reference: IkuraDecoder::EventGameDialog(VD_TITLE),
// src/ikura/ikuradecoder.cpp - LoadScript("TITLE", "ISF")).
//
// cabinet is not owned and must outlive the returned Context - IOP_LS/
// IOP_LSBS look up other named scripts through it for as long as the
// script keeps running (see Context::setScriptCabinet, wired up here).
// Returns nullptr if cabinet is null or has no TITLE.ISF entry.
VM::Context *loadTitleScript(Common::Archive *cabinet);

} // End of namespace Ikura

#endif
