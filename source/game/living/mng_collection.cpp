#include "shared.h"
#include "flash/entities/flash_entity.h"
#include "mng_collection.h"

MngCollection::MngCollection() : Manager() {
    // FUN_009d1700
}

MngCollection::~MngCollection() {
    // FUN_0042d6a0 (scalar deleting dtor)
}

void MngCollection::terminate() {
    // FUN_009d1be0
    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (collection screen)
}

bool MngCollection::onMessage( FlashMessage * /*pMessage*/ ) {
    // FUN_009d2ac0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (collection screen)
    return false;
}
