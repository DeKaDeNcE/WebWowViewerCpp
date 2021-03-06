//
// Created by Deamon on 8/27/2018.
//

#ifndef AWEBWOWVIEWERCPP_IUNIFORMBUFFER_H
#define AWEBWOWVIEWERCPP_IUNIFORMBUFFER_H

class IUniformBuffer {
private:
    void bind(int bindingPoint, int offset, int length); //Should be called only by GDevice

public:
    virtual ~IUniformBuffer() {};
    virtual void createBuffer() = 0;
};

#endif //AWEBWOWVIEWERCPP_IUNIFORMBUFFER_H
