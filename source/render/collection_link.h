#pragma once

#include <iterator>
#include <list>

// Removal handle for FileCollection3D to reduce the cost of resource removal on
// sector unstream (std::list::remove is O(N))
template <typename T> struct CollectionLink {
    void attach( std::list<T *> &param_1, T *param_2 ) {
        param_1.emplace_back( param_2 );
        pOwnerList = &param_1;
        Position = std::prev( param_1.end() );
    }

    bool detach( std::list<T *> &param_1 ) {
        if ( pOwnerList != &param_1 ) {
            return false;
        }

        param_1.erase( Position );
        pOwnerList = nullptr;
        return true;
    }

    std::list<T *> *pOwnerList = nullptr;
    typename std::list<T *>::iterator Position;
};
