//
// Created by Deamon on 9/5/2017.
//

#ifndef WOWVIEWERLIB_IWMOAPI_H
#define WOWVIEWERLIB_IWMOAPI_H

#include "m2Object.h"

class IWmoApi {
public:
    virtual M2Object *getDoodad(int index) = 0;
};
#endif //WOWVIEWERLIB_IWMOAPI_H